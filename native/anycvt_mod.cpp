// AnyCVT: a belt CVT for the small, medium and large engine. Each one is the stock gearbox of that size (same case,
// same engine face, same mass) with the small gearbox's shifter link on top, and the link steps it R, N, D. In D it
// slides between the low and high ratio to hold the engine at the target RPM (lower at part throttle) and its
// centrifugal clutch bites at the engage RPM; R uses the low ratio backwards. Low ratio, high ratio, target RPM,
// response and engage RPM are Properties rows; outputs ratio, engine_rpm, target_rpm, selector and clutch_factor.
//
// How: the definitions are built from the game's own gearbox, gearbox_b and gearbox_c definitions when the game starts
// (anycvt_logic.h), written to "AnyAPI and Modding/AnyCVT" and added to the game's part definitions after the game
// adds its own (as GpsPart, AnyBattery and AnyAlternator do). Their class is "gearbox", so the engine drives them like
// a stock gearbox. A hook on the gearbox tick runs AnyCVT's tick for its own parts in place of the stock one: it reads
// the shifter link, slides the ratio and sets the torque node's ratio and clutch. The engine speed and throttle come
// from a hook on the engine tick, which notes them for every CVT on its faces. Settings live in the gearbox's own
// saved ratio slots. A hook on get_gear_count hides the stock gear ratio rows and one on get_properties adds AnyCVT's;
// on the client a hook on the s32 row UI gives those rows their names. Stock gearboxes pass through untouched.
//
// Co-op: the CVTs run on the host, like every stock part; every player needs the mod for the parts to exist on their
// side. A player without the client hooks sees the rows under stock names (Gear Ratio 1, Gear Ratio 2, Max,
// Stiffness, Min).
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anycvt_bindings.h): Anymaker 0.1.28 / Steam build
// 26277967 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "anycvt_bindings.h"
#include "route_cell.h"
#include "anycvt_logic.h"
#include "anycvt_mesh.h"
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

namespace anycvt {
namespace {
constexpr const char* kModId = "anycvt";

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
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t vehicle_id = 64;                 // server_scene.vehicle.m_id (s32, saved)
// server_scene.vehicle_component.gearbox
constexpr ptrdiff_t torque_nodes = 0x1e0;            // m_torque_data.m_nodes (array<vehicle_torque_node>); node 0 is "torque"
constexpr ptrdiff_t gear_shift_value = 0x220 + 0x18; // m_logic_node_gear_shift._m_value (f64)
constexpr ptrdiff_t input_factor = 0x270;            // s32, Properties "Input"
constexpr ptrdiff_t output_factor = 0x274;           // s32, Properties "Output"
constexpr ptrdiff_t gear_ratios = 0x278;             // array<s32> (8, saved gear_ratio_1..8): AnyCVT's settings
constexpr ptrdiff_t current_gear = 0x298, target_gear = 0x29c;   // s32; -1 is neutral (the tooltip shows "--")
constexpr ptrdiff_t clutch_factor = 0x2a0;           // f64, output clutch_factor and the tooltip
constexpr ptrdiff_t is_reverse = 0x2ac;              // bool, Properties "Reverse"
// server_scene.vehicle_torque_node (104 bytes)
constexpr size_t node_size = 104;
constexpr ptrdiff_t node_component = 0, node_rps = 0x10, node_gear_ratio = 0x30, node_clutch = 0x38, node_adjacent = 0x48;
// server_scene.vehicle_component.engine
constexpr ptrdiff_t engine_gearboxes = 0x470;        // m_gearboxes (vector<ptr<gearbox>>)
constexpr ptrdiff_t engine_rps = 0x590;              // m_rps (f64)
constexpr ptrdiff_t engine_throttle = 0x5c0;         // m_display_throttle (f64, written in its tick)
// client_scene.peer.property_data.property_s32_range (as EngineSound reads it)
constexpr ptrdiff_t row_label = 16 + 8, row_min = 64 + 8, row_max = 80 + 8;
}  // namespace off
constexpr int32_t kStoreSystem = 1;
constexpr int kAddPropertyS32RangeSlot = 4;          // server_scene.peer.property_data: add_property_s32 (label, ptr<s32>, min, max)
constexpr int kEngineFreshTicks = 4;                 // an engine that has not driven the CVT for longer has gone

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
int component_size(const void* component) {
    if (!component || !can_read(static_cast<const uint8_t*>(component) + off::component_definition, 8)) return -1;
    return definition_size(*reinterpret_cast<const void* const*>(static_cast<const uint8_t*>(component) + off::component_definition));
}
// The gearbox's 8 saved ratio slots, or nullptr when the array is not as expected.
int32_t* slots_of(void* gearbox) {
    auto* a = reinterpret_cast<anymaker::gc_array_raw*>(static_cast<uint8_t*>(gearbox) + off::gear_ratios);
    if (!can_read(a, sizeof *a) || a->count < kSlots || a->element_size != 4 || !can_read(a->buffer, kSlots * 4)) return nullptr;
    return reinterpret_cast<int32_t*>(a->buffer);
}
uint8_t* torque_node_of(void* gearbox) {
    auto* a = reinterpret_cast<anymaker::gc_array_raw*>(static_cast<uint8_t*>(gearbox) + off::torque_nodes);
    if (!can_read(a, sizeof *a) || a->count < 1 || a->element_size != off::node_size || !can_read(a->buffer, off::node_size)) return nullptr;
    return a->buffer;
}

// ================================================================== files: meshes and definitions
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
    std::wstring rom = dir.substr(0, dir.find_last_of(L"\\/")) + L"\\rom\\";
    std::wstring components = rom + L"meshes\\components\\";
    std::vector<MeshVertex> connector = shifter_connector(read_file(components + wide(kSizes[0].stock_mesh) + L".mesh"));
    if (connector.empty()) log(1, "the shifter connector was not found on gear_box_a.mesh; the medium and large CVTs look like the stock gearboxes without it");
    std::string meshes[kSizeCount];
    for (int i = 0; i < kSizeCount; ++i) {
        const Size& s = kSizes[i];
        meshes[i] = stock_mesh_path(s);
        if (!s.mesh || connector.empty()) continue;
        std::vector<uint8_t> stock = read_file(components + wide(s.stock_mesh) + L".mesh");
        std::vector<uint8_t> bytes = build_mesh(stock, connector, s);
        if (!bytes.empty() && write_file(components + wide(s.mesh) + L".mesh", bytes.data(), bytes.size())) meshes[i] = mesh_path(s);
        else log(1, "could not build %s.mesh from %s.mesh (%s); the %s uses the stock look", s.mesh, s.stock_mesh,
                 stock.empty() ? "not found" : bytes.empty() ? "not the expected shape" : "not writable", s.name);
    }
    std::vector<uint8_t> stock_defs = read_file(rom + L"data\\vehicle_component_definitions.json");
    std::string error;
    std::string json = definitions_json(std::string_view(reinterpret_cast<const char*>(stock_defs.data()), stock_defs.size()), meshes, &error);
    if (json.empty()) { log(2, "could not build the CVT definitions: %s", stock_defs.empty() ? "rom\\data\\vehicle_component_definitions.json not found" : error.c_str()); return false; }
    dir += L"\\AnyCVT";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anycvt_parts.json";
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
// Other mods' hooks may sit under ours on this cell; calling down through them is fine because every mod checks
// whether its parts are already in the container before adding them.
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
        log(ok ? 0 : 2, ok ? "Small, Medium and Large CVT added to the game's parts (%s)" : "could not load the CVT definitions from %s", g_json_path.c_str());
}

// ================================================================== per part (server)
// Data slots are never freed: microcontrollers keep the pointer get_data_f64 hands them, so it must outlive a world
// change. A new part at a reused address gets the slot back with its defaults.
struct Live {
    int32_t vehicle = 0, component = 0;
    bool seen = false;
    Shifter shifter;
    State state;
    double engine_rps = 0, throttle = 0;   // from the engine tick
    int engine_age = 1 << 20;              // ticks since an engine last drove it
    double ratio = 0, engine_rpm = 0, target_rpm = 0, selector = 0;   // outputs
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

// The engine tick: after the game's, note its speed and throttle for every CVT on its faces (only ever read here,
// while the engine is alive).
using tick_t = void (*)(void* self, void* vehicle, void* scene);
anymaker::cell_hook h_engine_tick;
void hk_engine_tick(void* self, void* vehicle, void* scene) {
    reinterpret_cast<tick_t>(h_engine_tick.original)(self, vehicle, scene);
    if (!g_ready.load() || !self || !can_read(static_cast<uint8_t*>(self) + off::engine_throttle, 8)) return;
    auto* boxes = reinterpret_cast<const anymaker::gc_vector_raw*>(static_cast<uint8_t*>(self) + off::engine_gearboxes);
    if (!can_read(boxes, sizeof *boxes) || boxes->count <= 0 || boxes->count > 16) return;
    double rps = at<double>(self, off::engine_rps), throttle = at<double>(self, off::engine_throttle);
    for (int32_t i = 0; i < boxes->count; ++i) {
        void** p = boxes->at<void*>(i);
        void* box = p && can_read(p, 8) ? *p : nullptr;
        if (!box || component_size(box) < 0) continue;
        std::lock_guard lock(g_mutex);
        if (Live* live = live_of(box)) { live->engine_rps = rps; live->throttle = throttle; live->engine_age = 0; }
    }
}

// The gearbox tick, in place of the stock one for our parts.
anymaker::cell_hook h_tick;
void hk_tick(void* self, void* vehicle, void* scene) {
    auto original = reinterpret_cast<tick_t>(h_tick.original);
    int size_index = g_ready.load() ? component_size(self) : -1;
    if (size_index < 0 || !vehicle || !can_read(static_cast<uint8_t*>(vehicle) + off::vehicle_id, 4)) return original(self, vehicle, scene);
    int32_t* slots = slots_of(self);
    uint8_t* node = torque_node_of(self);
    if (!slots || !node) {   // the stock tick would read AnyCVT's settings as gear ratios: leave it in neutral instead
        static std::atomic<int> logs{3};
        if (logs.fetch_sub(1) > 0) log(1, "a %s's ratio slots or torque node are not as expected; it stays in neutral", kSizes[size_index].name);
        if (node) { at<double>(node, off::node_gear_ratio) = 1.0; at<double>(node, off::node_clutch) = 0.0; }
        return;
    }
    double dt = g_delta_time && std::isfinite(*g_delta_time) && *g_delta_time > 0 ? *g_delta_time : 1.0 / 60.0;
    int32_t vehicle_id = at<int32_t>(vehicle, off::vehicle_id), component_id = at<int32_t>(self, off::component_id);
    bool fresh = ensure_defaults(slots);
    Settings settings = read_settings(slots);
    Inputs in;
    in.dt = dt;
    std::unique_lock lock(g_mutex);
    Live* live = live_of(self);
    if (!live) { lock.unlock(); return original(self, vehicle, scene); }
    if (!live->seen || live->vehicle != vehicle_id || live->component != component_id) {
        double engine_rps = live->engine_rps, throttle = live->throttle;
        int engine_age = live->engine_age;
        *live = Live{};                              // a new part (or one at a freed address); keep a note the engine made this tick
        live->seen = true; live->vehicle = vehicle_id; live->component = component_id;
        live->engine_rps = engine_rps; live->throttle = throttle; live->engine_age = engine_age;
        static std::atomic<int> logs{12};
        if (logs.fetch_sub(1) > 0)
            log(0, "%s %d:%d%s: ratio %.0f to %.0f, target %.0f rpm, engage %.0f rpm, response %.0f", kSizes[size_index].name, vehicle_id, component_id,
                fresh ? " (new, defaults)" : "", settings.low, settings.high, settings.target_rpm, settings.engage_rpm, settings.response);
    }
    int selector = step_selector(live->shifter, clamp_selector(slots[kSelectorSlot]), at<double>(self, off::gear_shift_value));
    slots[kSelectorSlot] = selector;
    in.selector = selector;
    if (live->engine_age < (1 << 20)) ++live->engine_age;
    in.engine = live->engine_age <= kEngineFreshTicks;
    in.engine_rpm = std::isfinite(live->engine_rps) ? std::fabs(live->engine_rps) * 60.0 : 0.0;
    in.throttle = live->throttle;
    void* adjacent = at<void*>(node, off::node_adjacent);
    in.output_rps = adjacent && can_read(static_cast<uint8_t*>(adjacent) + off::node_rps, 8) ? at<double>(adjacent, off::node_rps) : 0.0;
    int32_t input = at<int32_t>(self, off::input_factor), output = at<int32_t>(self, off::output_factor);
    in.direction = (input != 0 ? double(output) / double(input) : 1.0) * (at<bool>(self, off::is_reverse) ? -1.0 : 1.0);
    Outputs o = step(settings, live->state, in);
    live->ratio = o.ratio; live->engine_rpm = in.engine_rpm; live->target_rpm = o.goal_rpm; live->selector = selector;
    lock.unlock();
    // What the stock tick ends with, from AnyCVT's numbers (set_gear_ratio and set_clutch_factor only store them).
    at<double>(node, off::node_gear_ratio) = o.node_ratio;
    at<double>(node, off::node_clutch) = o.clutch;
    at<double>(self, off::clutch_factor) = o.clutch;
    at<int32_t>(self, off::current_gear) = -1;       // no stock gear: the stock tooltip shows "--" for gear and ratio
    at<int32_t>(self, off::target_gear) = -1;
}

using get_f64_t = void (*)(double** ret, void* self, const anymaker::gc_string_view* name);
anymaker::cell_hook h_get_f64;
void hk_get_f64(double** ret, void* self, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, self, name);
    if (!ret || *ret || !self || !name || component_size(self) < 0) return;
    std::string_view n = name->view();
    std::lock_guard lock(g_mutex);
    Live* live = live_of(self);
    if (!live) return;
    if (n == "ratio") *ret = &live->ratio;
    else if (n == "engine_rpm") *ret = &live->engine_rpm;
    else if (n == "target_rpm") *ret = &live->target_rpm;
    else if (n == "selector") *ret = &live->selector;
}

// ================================================================== Properties (server)
// No stock gear ratio rows for a CVT (get_gear_count 0), then AnyCVT's five rows on its saved slots after the stock
// name, Input, Output and Reverse rows.
using gear_count_t = void (*)(int32_t* ret, const void* self);
anymaker::cell_hook h_gear_count;
void hk_gear_count(int32_t* ret, const void* self) {
    reinterpret_cast<gear_count_t>(h_gear_count.original)(ret, self);
    if (ret && g_ready.load() && component_size(self) >= 0) *ret = 0;
}

using get_properties_t = void (*)(void* self, void* properties);
using add_s32_range_t = void (*)(void* properties, const int32_t* label, int32_t* const* value, const int32_t* min, const int32_t* max);
anymaker::cell_hook h_properties;
void* container_method(void* container, int slot) {
    if (!container || !anymaker::readable(container, 8)) return nullptr;
    auto ti = anymaker::typeinfo_of(container);
    if (!ti.p || !anymaker::readable(ti.p + 0x80, 8) || !ti.table() || !anymaker::readable(ti.table() + slot, 8)) return nullptr;
    return ti.method(slot);
}
void hk_properties(void* self, void* properties) {
    reinterpret_cast<get_properties_t>(h_properties.original)(self, properties);
    int size_index = g_ready.load() ? component_size(self) : -1;
    int32_t* slots = size_index >= 0 ? slots_of(self) : nullptr;
    if (!slots || !properties) return;
    auto add = reinterpret_cast<add_s32_range_t>(container_method(properties, kAddPropertyS32RangeSlot));
    if (!add) { log(1, "Properties rows not added (the row method was not found)"); return; }
    ensure_defaults(slots);
    for (const Setting& s : kSettings) {
        int32_t* value = slots + s.slot;
        add(properties, &s.label, &value, &s.min, &s.max);
    }
    static std::atomic<int> logs{4};
    if (logs.fetch_sub(1) > 0) log(0, "%s Properties rows added", kSizes[size_index].name);
}

// ================================================================== row names (client)
// The s32 row UI asks the localization for its label's text; for AnyCVT's rows it gets AnyCVT's name instead.
using row_ui_t = void (*)(uint8_t* row, void* frontend_ui, uint8_t* client, const int32_t* index, const int32_t* count);
using get_text_t = void (*)(anymaker::gc_string_view* ret, const void* localization, const int32_t* id);
anymaker::cell_hook h_row, h_text;
thread_local const char* t_text = nullptr;
thread_local int32_t t_label = -1;
void hk_row(uint8_t* row, void* frontend_ui, uint8_t* client, const int32_t* index, const int32_t* count) {
    auto original = reinterpret_cast<row_ui_t>(h_row.original);
    int setting = row && can_read(row + off::row_max, 4) ? setting_of_row(at<int32_t>(row, off::row_label), at<int32_t>(row, off::row_min), at<int32_t>(row, off::row_max)) : -1;
    if (setting < 0) return original(row, frontend_ui, client, index, count);
    const char* prev_text = t_text; int32_t prev_label = t_label;
    t_text = kSettings[setting].text; t_label = kSettings[setting].label;
    original(row, frontend_ui, client, index, count);
    t_text = prev_text; t_label = prev_label;
}
void hk_text(anymaker::gc_string_view* ret, const void* localization, const int32_t* id) {
    if (t_text && ret && id && *id == t_label && g_string_ctor) { g_string_ctor(ret, t_text); return; }   // a static name: the view outlives the label
    reinterpret_cast<get_text_t>(h_text.original)(ret, localization, id);
}

// ================================================================== install (background thread)
void remove_hooks() {
    h_text.remove((void*)&hk_text);
    h_row.remove((void*)&hk_row);
    h_properties.remove((void*)&hk_properties);
    h_gear_count.remove((void*)&hk_gear_count);
    h_get_f64.remove((void*)&hk_get_f64);
    h_add_definitions.remove((void*)&hk_add_definitions);
    h_engine_tick.remove((void*)&hk_engine_tick);
    h_tick.remove((void*)&hk_tick);
}
void install_hooks() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyCVT stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        void** tick = route::cell(bind::gearbox_tick);
        void** engine_tick = route::cell(bind::engine_tick);
        if (add && !route::add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (load && !route::file_load_ok((void*)load)) load = nullptr;
        if (!add || !ctor || !dtor || !load || !sc || !tick || !engine_tick) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d file ctor=%d dtor=%d load=%d string=%d gearbox tick=%d engine tick=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!ctor, !!dtor, !!load, !!sc, !!tick, !!engine_tick);
            continue;
        }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        if (!g_get_definition) { log(2, "definition lookup not found; AnyCVT is off"); return; }
        auto dt = static_cast<const double*>(global(bind::server_delta_time));
        g_delta_time = dt && anymaker::readable(dt, 8) ? dt : nullptr;
        if (!write_files()) { log(2, "could not write the AnyCVT definition file; AnyCVT is off"); return; }
        // The tick hooks go in before the parts exist: without them a CVT would be a stock gearbox with odd ratios.
        if (!h_tick.install(tick, (void*)&hk_tick) || !h_engine_tick.install(engine_tick, (void*)&hk_engine_tick)) {
            log(2, "gearbox or engine tick hook not installed (cell taken or not writable); AnyCVT is off");
            remove_hooks();
            return;
        }
        if (!h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            log(2, "the definitions hook cell was already taken or not writable; AnyCVT is off");
            remove_hooks();
            return;
        }
        void** data = route::cell(bind::gearbox_get_data_f64);
        if (!data || !h_get_f64.install(data, (void*)&hk_get_f64)) log(1, "ratio, engine_rpm, target_rpm and selector unavailable (clutch_factor still works)");
        void** count = route::cell(bind::gearbox_get_gear_count);
        void** props = route::cell(bind::gearbox_get_properties);
        // The gear count hook goes in only with the rows hook: without AnyCVT's rows the stock ones stay.
        if (!count || !props || !h_properties.install(props, (void*)&hk_properties) || !h_gear_count.install(count, (void*)&hk_gear_count)) {
            h_properties.remove((void*)&hk_properties);
            log(1, "Properties rows not hooked; a CVT shows the stock Gear Ratio 1 and 2 rows (its low and high ratio) and its defaults for the rest");
        }
        void** row = route::cell(bind::client_property_s32_range_update_ui);
        void** text = route::cell(bind::localization_get);
        // The text hook goes in first: it does nothing until the row hook marks a row.
        if (!row || !text || !h_text.install(text, (void*)&hk_text) || !h_row.install(row, (void*)&hk_row)) {
            h_text.remove((void*)&hk_text);
            log(1, "row names not hooked; AnyCVT's rows show as Gear Ratio 1, Gear Ratio 2, Max, Stiffness and Min");
        }
        g_ready = true;
        log(0, "Ready after %d s (server tick length %s). Load or start a world to add the CVTs (definition file %s).", attempt,
            g_delta_time ? "found" : "not found, assuming 60 Hz", g_json_path.c_str());
        return;
    }
}
}  // namespace
}  // namespace anycvt

using namespace anycvt;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
