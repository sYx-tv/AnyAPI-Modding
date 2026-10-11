// GpsPart: a new "GPS Sensor" part, built from the stock compass sensor (same class, mesh and ports).
//
// Data port and microcontroller outputs: x, y, z (the part's world position, y is up), altitude, heading
// (the stock compass north angle, 0-360), pitch, roll, speed / ground speed / vertical speed in m/s and km/h,
// acceleration in m/s² and km/h per second, and distance, horizontal distance, bearing, relative bearing and
// height difference to a waypoint. Inputs waypoint_x, waypoint_y and waypoint_z set the waypoint (world
// coordinates). north_angle still works as on the compass.
//
// Getting it: the part is a vehicle component with tech_tier 4 in the
// "sensor" category (the sandbox and creative sensor container). World loot spawns items whose tech_tier equals
// the loot level being generated; a new world starts at loot level 1 and each bunker's end sequence
// (dungeon_<n>_end_sequence -> dungeon_manager.complete_dungeon) raises it by one, so tier 4 loot starts
// generating the moment the 3rd bunker is triggered to explode (game code, Anymaker 0.1.24). Single player only for now: the definition
// is added on both the server and the client scene of this game, and every player would need the mod.
//
// How: the part definition is embedded below (gpspart_logic.h), written to "AnyAPI and Modding/GpsPart" and added
// to the game's vehicle component definitions right after the game adds its own. The compass sensor's server
// tick and get_data_f64 are hooked; they only change anything for parts whose definition id is gps_sensor.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (gpspart_bindings.h): Anymaker 0.1.24 /
// Steam build 25826614 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_experimental.hpp"
#include "gpspart_bindings.h"
#include "gpspart_logic.h"
#include "gpspart_help.h"
#include "gpspart_mesh.h"
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

namespace gpspart {
namespace {
constexpr const char* kModId = "gpspart";

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

// Reference layouts (metadata).
namespace off {
constexpr ptrdiff_t component_definition = 312;     // server_scene.vehicle_component.m_definition (ptr<const vehicle_component_definition>)
constexpr ptrdiff_t compass_angle_to_north = 536;   // server_scene.vehicle_component.compass_sensor.m_angle_to_north (f64)
constexpr ptrdiff_t definition_id = 0;              // vehicle_component_definition.m_id (string)
constexpr size_t definition_file_size = 40;         // vehicle_component_definition_file
constexpr ptrdiff_t scene_loot_level = 5336;        // server_scene.m_loot_level (s32)
}  // namespace off
constexpr int32_t kStoreSystem = 1;                 // file.e_store.system (static inference)

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;

// file.path { e_store m_store; string m_path; } (24 bytes)
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");

// ================================================================== part definition
using add_definitions_t = void (*)(void* container, const void* file);
using file_ctor_t = void (*)(void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
anymaker::cell_hook h_add_definitions;
file_ctor_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
std::string g_json_path;                            // absolute, backslashes
std::thread::id g_adding;                           // the thread currently adding our file (re-entry guard)

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

// Builds the GPS mesh (compass mesh + forward arrow) into rom/meshes/components beside the stock mesh, where the
// game loads part meshes from. Returns the mesh path to put in the definition: the stock compass mesh on failure.
std::string build_mesh(const std::wstring& game_dir) {
    std::wstring components = game_dir + L"\\rom\\meshes\\components\\";
    std::vector<uint8_t> stock = read_file(components + L"compass_sensor_a.mesh");
    std::vector<uint8_t> gps = build_gps_mesh(stock, "gpspart_gps_sensor_a");
    if (gps.empty()) { log(1, "compass mesh not recognised (%zu bytes); the GPS Sensor uses the stock compass look", stock.size()); return kStockMeshPath; }
    if (!write_file(components + L"gpspart_gps_sensor_a.mesh", gps.data(), gps.size())) { log(1, "could not write the GPS Sensor mesh; using the stock compass look"); return kStockMeshPath; }
    return kGpsMeshPath;
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
    std::string mesh = build_mesh(game_dir);
    dir += L"\\GpsPart";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\gps_sensor.json";
    std::string json = definition_json(mesh);
    bool ok = write_file(file, json.data(), json.size());
    log(0, "GPS Sensor mesh: %s", mesh.c_str());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    return ok && !g_json_path.empty();
}

// Loads our JSON into a fresh vehicle_component_definition_file and hands it to the container. The path's
// characters must outlive the call ($string_ctor_cstr makes a non-owning view), so they are static.
bool add_ours(void* container, int32_t store, const std::string& text) {
    static std::string kept[4];
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

void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container) return;
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, kStoreSystem, g_json_path) || add_ours(container, kStoreSystem, forward);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0) log(ok ? 0 : 2, ok ? "GPS Sensor part added to the game's parts (%s)" : "could not load the GPS Sensor definition from %s", g_json_path.c_str());
}

// ================================================================== host side (server thread)
using tick_t = void (*)(uint8_t* sensor, void* vehicle, void* scene);
using get_f64_t = void (*)(double** ret, uint8_t* sensor, const anymaker::gc_string_view* name);
using transform_t = void (*)(double* out, const uint8_t* object);
anymaker::cell_hook h_tick, h_get_f64;
transform_t g_vehicle_transform, g_component_transform;

std::unordered_map<const uint8_t*, Sensor> g_sensors;   // keyed by server component; nodes keep their address
std::unordered_map<const void*, bool> g_is_gps;         // definition pointer -> gps_sensor?
std::mutex g_mutex;                                     // tick and data lookups may run on different threads

bool is_gps(const uint8_t* sensor) {
    if (!sensor || !anymaker::readable(sensor + off::component_definition, 8)) return false;
    auto* definition = *reinterpret_cast<const uint8_t* const*>(sensor + off::component_definition);
    if (!definition) return false;
    auto found = g_is_gps.find(definition);
    if (found != g_is_gps.end()) return found->second;
    bool gps = false;
    if (anymaker::readable(definition + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(definition + off::definition_id);
        gps = id->data && id->length > 0 && id->length < 64 && anymaker::readable(id->data, size_t(id->length)) && id->view() == kDefinitionId;
    }
    g_is_gps[definition] = gps;
    return gps;
}

void hk_tick(uint8_t* sensor, void* vehicle, void* scene) {
    reinterpret_cast<tick_t>(h_tick.original)(sensor, vehicle, scene);
    if (!sensor || !vehicle) return;
    std::lock_guard lock(g_mutex);
    if (!is_gps(sensor)) return;
    Mat34 v, c;
    g_vehicle_transform(v.m, static_cast<const uint8_t*>(vehicle));
    g_component_transform(c.m, sensor);
    Mat34 world = compose(v, c);
    if (!finite(world)) return;
    double north = *reinterpret_cast<const double*>(sensor + off::compass_angle_to_north);
    auto& s = g_sensors[sensor];
    bool was_known = s.sign_known;
    tick(s, world, north, now_seconds());
    static std::atomic<int> logs{4};
    if (logs.fetch_sub(1) > 0) {
        auto* lvl = static_cast<uint8_t*>(scene) + off::scene_loot_level;
        log(0, "GPS sensor at (%.1f, %.1f, %.1f) heading %.1f pitch %.1f roll %.1f speed %.2f m/s, world loot level %d", s.slots[kX], s.slots[kY], s.slots[kZ],
            s.slots[kHeading], s.slots[kPitch], s.slots[kRoll], s.slots[kSpeedMs], scene && anymaker::readable(lvl, 4) ? *reinterpret_cast<int32_t*>(lvl) : -1);
    }
    if (!was_known && s.sign_known) log(0, "heading direction calibrated (sign %+.0f)", s.sign);
}

void hk_get_f64(double** ret, uint8_t* sensor, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, sensor, name);
    if (!ret || *ret || !sensor || !name) return;
    auto view = name->view();
    int i = channel_index(view.data(), view.size());
    if (i < 0) return;
    std::lock_guard lock(g_mutex);
    if (!is_gps(sensor)) return;
    *ret = &g_sensors[sensor].slots[i];
    static std::atomic<int> logs{8};
    if (logs.fetch_sub(1) > 0) log(0, "data channel %s connected", kChannels[i].name);
}

// ================================================================== microcontroller help panel
// frontend_ui_microcontroller.update_ui runs every client frame while the microcontroller editor is open, so a
// recent call means the editor is on screen. The button and panel are drawn over it with anyapi.gpu_draw.
using update_ui_t = void (*)(void*, void*, void*, void*, void*, void*, void*, void*);
anymaker::cell_hook h_update_ui;
std::atomic<double> g_editor_seen{-1e9};
const AnyGpuDrawV1* gpu;
std::mutex g_help_mutex;                               // input and render run on different threads
bool g_help_open = false, g_help_escape_held = false;
float g_frame_w = 0, g_frame_h = 0;

void hk_update_ui(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h) {
    g_editor_seen = now_seconds();
    reinterpret_cast<update_ui_t>(h_update_ui.original)(a, b, c, d, e, f, g, h);
}
bool editor_open() { return now_seconds() - g_editor_seen.load() < 0.3; }

void draw_help(const AnyFrameV1* frame, void*) {
    if (!frame || !gpu) return;
    std::lock_guard lock(g_help_mutex);
    g_frame_w = float(frame->width); g_frame_h = float(frame->height);
    if (!editor_open()) { g_help_open = false; return; }
    HelpLayout l = help_layout(g_frame_w, g_frame_h);
    float s = l.s;
    auto box = [&](const Box& b, uint32_t color, float radius, uint32_t flags = 0, float stroke = 1) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_ROUND_RECT; c.rect[0] = b.x; c.rect[1] = b.y; c.rect[2] = b.w; c.rect[3] = b.h;
        c.color = color; c.radius = radius; c.flags = flags; c.stroke = stroke; gpu->emit(&c);
    };
    auto text = [&](float x, float y, float w, float h, const wchar_t* t, float size, uint32_t color, uint32_t flags = 0) {
        AnyGpuCommandV1 c; c.kind = ANY_GPU_TEXT; c.rect[0] = x; c.rect[1] = y; c.rect[2] = w; c.rect[3] = h;
        c.text = t; c.text_length = uint32_t(wcslen(t)); c.font_size = size; c.color = color;
        c.flags = flags | ANY_GPU_NOWRAP | ANY_GPU_VCENTER; gpu->emit(&c);
    };
    const uint32_t yellow = 0xffffc400, panel = 0xf0191f25, line = 0xff3a434d, light = 0xffe8ecef, dim = 0xffaab4bd;
    box(l.button, g_help_open ? 0xff2b333b : panel, 6 * s);
    box(l.button, yellow, 6 * s, ANY_GPU_STROKE, 1.5f * s);
    text(l.button.x, l.button.y, l.button.w, l.button.h, L"?  GPS outputs", 15 * s, yellow, ANY_GPU_BOLD | ANY_GPU_CENTER);
    if (!g_help_open) return;
    box(l.panel, panel, 8 * s);
    box(l.panel, line, 8 * s, ANY_GPU_STROKE, 1 * s);
    float pad = 16 * s;
    text(l.panel.x + pad, l.panel.y + 8 * s, l.panel.w - 2 * pad, 30 * s, L"GPS Sensor data channels", 18 * s, yellow, ANY_GPU_BOLD);
    text(l.close.x, l.close.y, l.close.w, l.close.h, L"×", 20 * s, light, ANY_GPU_CENTER);
    float font = std::min(14 * s, l.row_h * .72f);
    for (int r = 0; r < kHelpRows; ++r) {
        float y = l.rows_y + r * l.row_h;
        if (r % 2) box({l.panel.x + 6 * s, y, l.panel.w - 12 * s, l.row_h}, 0x18ffffff, 3 * s);
        std::wstring name = r == 0 ? L"north_angle" : std::wstring(kChannels[r - 1].name, kChannels[r - 1].name + strlen(kChannels[r - 1].name));
        const wchar_t* help = r == 0 ? kNorthAngleHelp : kChannels[r - 1].help;
        bool input = r > 0 && kChannels[r - 1].input;
        text(l.panel.x + pad, y, l.name_w, l.row_h, name.c_str(), font, input ? 0xff7cc8ff : light, ANY_GPU_BOLD);
        text(l.panel.x + pad + l.name_w, y, l.panel.w - 2 * pad - l.name_w, l.row_h, help, font, dim);
    }
    text(l.panel.x + pad, l.panel.y + l.panel.h - 30 * s, l.panel.w - 2 * pad, 26 * s,
         L"Yellow arrow = forward. Blue rows are inputs; positions are world metres, y is up.", font, dim);
}

uint32_t help_input(const AnyInputV1* e, void*) {
    if (!e || !gpu) return 0;
    std::lock_guard lock(g_help_mutex);
    if (e->kind == ANY_FOCUS_LOST) { g_help_escape_held = false; return 0; }
    if (e->kind == ANY_KEY_UP && e->key == VK_ESCAPE && g_help_escape_held) { g_help_escape_held = false; return 1; }
    if (!editor_open() || g_frame_h <= 0) return 0;
    HelpLayout l = help_layout(g_frame_w, g_frame_h);
    float x = float(e->x), y = float(e->y);
    bool on_button = l.button.contains(x, y), on_panel = g_help_open && l.panel.contains(x, y);
    switch (e->kind) {
    case ANY_KEY_DOWN:
        if (e->key == VK_ESCAPE && g_help_open) { g_help_open = false; g_help_escape_held = true; return 1; }
        return 0;
    case ANY_MOUSE_DOWN:
        if (on_button) { if (e->button == 1) g_help_open = !g_help_open; return 1; }
        if (on_panel) { if (e->button == 1 && l.close.contains(x, y)) g_help_open = false; return 1; }
        if (g_help_open && e->button == 1) g_help_open = false;   // a click elsewhere closes it and reaches the editor
        return 0;
    case ANY_MOUSE_UP: case ANY_MOUSE_WHEEL: case ANY_MOUSE_MOVE:
        return (on_button || on_panel) && e->kind != ANY_MOUSE_MOVE ? 1 : 0;
    default: return 0;
    }
}

// ================================================================== install (background thread)
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; GpsPart stays off"); return; }
        void** add = hook_cell(bind::definitions_add_definitions);
        void** tick_cell = hook_cell(bind::server_compass_tick);
        void** get_cell = hook_cell(bind::server_compass_get_data_f64);
        auto ctor = reinterpret_cast<file_ctor_t>(function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(function(bind::definition_file_load));
        auto vt = reinterpret_cast<transform_t>(function(bind::server_vehicle_get_transform));
        auto ct = reinterpret_cast<transform_t>(function(bind::server_component_get_transform));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        if (!add || !tick_cell || !get_cell || !ctor || !dtor || !load || !vt || !ct || !sc) {
            if (attempt == 179)
                log(2, "could not locate the game functions (add=%d tick=%d get=%d file ctor=%d dtor=%d load=%d transforms=%d/%d string=%d)",
                    !!add, !!tick_cell, !!get_cell, !!ctor, !!dtor, !!load, !!vt, !!ct, !!sc);
            continue;
        }
        if (!write_definition_file()) { log(2, "could not write the GPS Sensor definition file; GpsPart is off"); return; }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_vehicle_transform = vt; g_component_transform = ct; g_string_ctor = sc;
        bool ok = h_tick.install(tick_cell, (void*)&hk_tick) && h_get_f64.install(get_cell, (void*)&hk_get_f64) &&
                  h_add_definitions.install(add, (void*)&hk_add_definitions);
        if (!ok) { log(2, "a hook cell was already taken or not writable; GpsPart is off"); return; }
        void** ui_cell = hook_cell(bind::client_microcontroller_update_ui);
        if (!ui_cell || !h_update_ui.install(ui_cell, (void*)&hk_update_ui)) log(1, "microcontroller help button unavailable (editor hook not found)");
        else if (!gpu) log(1, "microcontroller help button unavailable (anyapi.gpu_draw missing)");
        g_ready = true;
        log(0, "Ready after %d s. Load or start a world to add the GPS Sensor part (definition file %s).", attempt, g_json_path.c_str());
        return;
    }
}
void remove_hooks() {
    h_update_ui.remove((void*)&hk_update_ui);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_get_f64.remove((void*)&hk_get_f64);
    h_tick.remove((void*)&hk_tick);
}
}  // namespace
}  // namespace gpspart

using namespace gpspart;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId; out->input = help_input;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    if (auto services = AnyAPI_Services()) {
        auto* g = static_cast<const AnyGpuDrawV1*>(services->query("anyapi.gpu_draw", 1));
        if (g && g->struct_size == sizeof(*g) && g->version == 1 && g->emit && g->register_renderer && g->register_renderer(draw_help, nullptr)) gpu = g;
    }
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
