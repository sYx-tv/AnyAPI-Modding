// AnyBattery: the Battery Pack, a battery you place and stretch to size like a liquid tank. It is placed empty and
// charges from the electric network (alternators, solar panels), like the stock batteries.
//
// How: the definition (anybattery_logic.h) is written to "AnyAPI and Modding/AnyBattery" and added to the game's part
// definitions after the game adds its own (as GpsPart and AnyMoto do); its mesh is generated (anybattery_mesh.h) into
// rom/meshes/components. The part is the game's own battery holder (class "battery") with no slot: the electric
// network already fills and drains the holder's content, and only the capacity, which the stock holder copies from
// the battery item in its slot, is missing. A hook on the holder's server tick gives a pack its capacity from its
// size (0.5 kWh per cell by default) and keeps its charge, which it saves in a file in the world's working folder
// (the stock holder saves its charge inside the battery item). The same hook applies the Mod Settings for crash
// damage and self-discharge, and a hook on its data outputs adds charge_percent. Stock batteries pass through
// untouched.
//
// Co-op: the charge lives on the host, like every stock battery; joiners see it in the tooltip. Every player needs
// the mod for the part to exist on their side.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (anybattery_bindings.h): Anymaker 0.1.27 /
// Steam build 26113663 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyhelpers_settings_v1.h"
#include "anyapi_experimental.hpp"
#include "anybattery_bindings.h"
#include "route_cell.h"
#include "anybattery_logic.h"
#include "anybattery_mesh.h"
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

namespace anybattery {
namespace {
constexpr const char* kModId = "anybattery";

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
bool can_read(const void* p, size_t n) { return p && anymaker::readable(p, n); }

// Reference layouts (0.1.27 metadata).
namespace off {
constexpr ptrdiff_t component_id = 64;               // server_scene.vehicle_component.m_id (s32, saved)
constexpr ptrdiff_t component_extend = 296;          // m_extend_size's value (vec3_s32): cells added by stretching
constexpr ptrdiff_t component_definition = 312;      // m_definition (ptr<const vehicle_component_definition>)
constexpr ptrdiff_t definition_id = 0;               // vehicle_component_definition.m_id (string)
constexpr ptrdiff_t vehicle_id = 64;                 // server_scene.vehicle.m_id (s32, saved)
constexpr ptrdiff_t battery_energy = 0x1e0;          // battery.m_electric_content.m_energy (f64, J)
constexpr ptrdiff_t battery_capacity = 0x1e8;        // .m_capacity (f64, J)
constexpr ptrdiff_t battery_pending_damage = 0x308;  // battery.m_is_pending_damage_accessory (bool)
}  // namespace off
constexpr int32_t kStoreSystem = 1;

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
string_ctor_t g_string_ctor;
using get_definition_t = void (*)(const void** ret, const void* container, const anymaker::gc_string_view* id);
get_definition_t g_get_definition;

// ================================================================== which parts are ours
// Keyed by definition pointer; every world loads its definitions anew, so the cache starts afresh on each add.
std::mutex g_definition_mutex;
std::unordered_map<const void*, bool> g_is_pack;
bool pack_definition(const void* definition) {
    if (!definition) return false;
    std::lock_guard lock(g_definition_mutex);
    auto found = g_is_pack.find(definition);
    if (found != g_is_pack.end()) return found->second;
    bool yes = false;
    if (can_read(static_cast<const uint8_t*>(definition) + off::definition_id, sizeof(anymaker::gc_string_view))) {
        auto* id = reinterpret_cast<const anymaker::gc_string_view*>(static_cast<const uint8_t*>(definition) + off::definition_id);
        if (id->data && id->length > 0 && id->length < 64 && can_read(id->data, size_t(id->length))) yes = is_pack_id(id->data, size_t(id->length));
    }
    g_is_pack[definition] = yes;
    return yes;
}
void forget_definitions() { std::lock_guard lock(g_definition_mutex); g_is_pack.clear(); }
bool is_pack(const void* component) {
    return component && can_read(static_cast<const uint8_t*>(component) + off::component_definition, 8) &&
           pack_definition(at<const void*>(component, off::component_definition));
}

// ================================================================== files: mesh and definition
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
    std::wstring mesh_file = game_dir + L"\\rom\\meshes\\components\\" + std::wstring(kMeshName, kMeshName + std::strlen(kMeshName)) + L".mesh";
    std::vector<uint8_t> bytes = encode_mesh(battery_pack(), kMeshName);
    bool mesh = write_file(mesh_file, bytes.data(), bytes.size());
    if (!mesh) log(1, "could not write %s.mesh into rom\\meshes\\components; the Battery Pack uses the liquid tank's look", kMeshName);
    dir += L"\\AnyBattery";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\anybattery_pack.json";
    std::string json = definition_json(mesh ? mesh_path() : std::string("meshes/components/fuel_tank_a_0_0_0.mesh"));
    bool ok = write_file(file, json.data(), json.size());
    int n = WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, nullptr, 0, nullptr, nullptr);
    g_json_path.assign(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file.c_str(), -1, g_json_path.data(), n, nullptr, nullptr);
    return ok && !g_json_path.empty();
}

// ================================================================== part definition
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
// Other mods' hooks (GpsPart, AnyMoto, AnyEngineParts) may sit under ours on this cell; calling down through them is
// fine because every mod checks whether its parts are already in the container before adding them.
void hk_add_definitions(void* container, const void* file) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file);
    if (!g_ready.load() || g_adding == std::this_thread::get_id() || !container || find_definition(container, kDefinitionId)) return;
    forget_definitions();   // a new set of definitions (a world load): pointers seen before may be reused
    std::string forward = g_json_path;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, g_json_path) || add_ours(container, forward);
    if (const void* d = find_definition(container, kDefinitionId)) pack_definition(d);
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(ok ? 0 : 2, ok ? "Battery Pack added to the game's parts (%s)" : "could not load the Battery Pack definition from %s", g_json_path.c_str());
}

// ================================================================== settings (Mod Settings, from AnyHelpers)
const AnyHelpersSettingsV1* g_settings_api;
uint64_t g_tokens[4]{};
uint64_t g_settings_revision = UINT64_MAX;
Settings g_settings;
void register_settings(const AnyServicesV1* services) {
    g_settings_api = services ? (const AnyHelpersSettingsV1*)services->query("anyhelpers.settings", 1) : nullptr;
    if (!g_settings_api || g_settings_api->struct_size != sizeof(*g_settings_api) || g_settings_api->version != 1 ||
        !g_settings_api->register_setting || !g_settings_api->get || !g_settings_api->revision) {
        g_settings_api = nullptr;
        log(1, "Mod Settings (AnyHelpers) not found; Battery Packs use 0.5 kWh per cell, stock crash damage, no self-discharge");
        return;
    }
    static const char* damage[] = {"Empties it (stock)", "Loses half", "Keeps its charge"};
    AnyModSettingV1 d;
    d.mod_id = kModId; d.mod_name = "AnyBattery";
    d.setting_id = "kwh_per_cell"; d.label = "Energy per cell (kWh)"; d.order = 0; d.kind = ANY_SETTING_NUMBER;
    d.default_number = kStockKwhPerCell; d.minimum = 0.1; d.maximum = 5; d.step = 0.1;
    d.description = "How much a Battery Pack holds per cell of its size. 0.5 matches the stock batteries (the smallest pack, 2 x 2 x 2 cells, holds 4 kWh).";
    g_tokens[0] = g_settings_api->register_setting(&d);
    d = AnyModSettingV1{};
    d.mod_id = kModId; d.mod_name = "AnyBattery";
    d.setting_id = "crash_damage"; d.label = "Crash damage"; d.order = 1; d.kind = ANY_SETTING_CHOICE;
    d.choices = damage; d.choice_count = 3; d.default_number = kDrainAll; d.minimum = 0; d.maximum = 2;
    d.description = "What a crash that damages a Battery Pack does to its charge. The stock batteries empty.";
    g_tokens[1] = g_settings_api->register_setting(&d);
    d = AnyModSettingV1{};
    d.mod_id = kModId; d.mod_name = "AnyBattery";
    d.setting_id = "self_discharge"; d.label = "Self-discharge (% per hour)"; d.order = 2; d.kind = ANY_SETTING_NUMBER;
    d.default_number = 0; d.minimum = 0; d.maximum = 50; d.step = 0.5;
    d.description = "Charge a Battery Pack loses by itself, as a percentage of what it holds, per hour of play. 0 keeps it.";
    g_tokens[2] = g_settings_api->register_setting(&d);
    d = AnyModSettingV1{};
    d.mod_id = kModId; d.mod_name = "AnyBattery";
    d.setting_id = "start_charge"; d.label = "Charge when placed (%)"; d.order = 3; d.kind = ANY_SETTING_NUMBER;
    d.default_number = 0; d.minimum = 0; d.maximum = 100; d.step = 5;
    d.description = "Charge of a newly placed Battery Pack. 0: placed dead, charge it yourself.";
    g_tokens[3] = g_settings_api->register_setting(&d);
}
// The host's settings rule: the packs tick where the world runs. Server tick thread.
void refresh_settings() {
    if (!g_settings_api) return;
    uint64_t revision = g_settings_api->revision();
    if (revision == g_settings_revision) return;
    g_settings_revision = revision;
    Settings s;
    AnySettingValueV1 v;
    if (g_tokens[0] && g_settings_api->get(g_tokens[0], &v)) s.kwh_per_cell = v.number;
    if (g_tokens[1] && g_settings_api->get(g_tokens[1], &v) && std::isfinite(v.number)) s.damage = DamageMode(int(v.number));
    if (g_tokens[2] && g_settings_api->get(g_tokens[2], &v)) s.drain_percent_per_hour = v.number;
    if (g_tokens[3] && g_settings_api->get(g_tokens[3], &v)) s.start_percent = v.number;
    g_settings = clamp(s);
    log(0, "settings: %.2f kWh per cell, crash damage %d, self-discharge %.1f%%/h, placed at %.0f%%", g_settings.kwh_per_cell, int(g_settings.damage),
        g_settings.drain_percent_per_hour, g_settings.start_percent);
}

// ================================================================== saved charge
// The game swaps the working folder's contents when a world loads, so the file is watched: a new file (or none) means
// another world. A gap in the packs' ticks (a loading screen) makes the next tick look at once.
ChargeMap g_saved;
std::wstring g_charge_path;                          // empty: charge is kept for this session only
struct FileStamp { bool exists = false; FILETIME created{}, written{}; uint64_t size = 0; };
FileStamp g_stamp;
bool g_dirty = false;
double g_polled = -1e9, g_written = -1e9, g_last_tick = -1e9;

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
void init_charge_path() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) { log(1, "APPDATA not set; Battery Pack charge is not saved"); return; }
    std::wstring dir = std::wstring(buf) + L"\\Anymaker\\working";
    DWORD attributes = GetFileAttributesW(dir.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && GetFileAttributesW((std::wstring(buf) + L"\\Anymaker").c_str()) == INVALID_FILE_ATTRIBUTES) {
        log(1, "the game's user folder (%%APPDATA%%\\Anymaker) was not found; Battery Pack charge is not saved");
        return;
    }
    g_charge_path = dir + L"\\anybattery_charge.json";
    log(0, "Battery Pack charge is saved with each world (working folder %s)", attributes == INVALID_FILE_ATTRIBUTES ? "not there yet" : "found");
}

struct Live { Pack pack; int32_t vehicle = 0, component = 0; double last_tick = 0; };
std::mutex g_mutex;                                  // g_live and g_percent (server tick and data reads)
std::unordered_map<const void*, Live> g_live;        // keyed by server component
// charge_percent for each pack, in slots that are never freed: microcontrollers keep the pointer the data output hands
// them, so it must outlive a world change. A part at a reused address gets the same slot.
std::unordered_map<const void*, double*> g_percent;
double* percent_slot(const void* component) {
    double*& slot = g_percent[component];
    if (!slot) slot = static_cast<double*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(double)));
    return slot;
}

// Reloads the charge when the world changed under us; writes ours every few seconds when it changed. Server tick.
void sync_charge(double now, bool force_poll) {
    if (g_charge_path.empty() || (!force_poll && now - g_polled < 0.5)) return;
    g_polled = now;
    FileStamp s = stamp_of(g_charge_path);
    if (!same_stamp(s, g_stamp)) {
        if (s.exists) {
            std::vector<uint8_t> text = read_file(g_charge_path);
            g_saved = decode_charge(std::string(text.begin(), text.end()));
            log(0, "Battery Pack charge loaded: %zu packs", g_saved.size());
        } else if (g_stamp.exists) {
            g_saved.clear();
            log(0, "Battery Pack charge file gone (another world); starting fresh");
        }
        g_stamp = s;
        g_dirty = false;
        std::lock_guard lock(g_mutex);
        g_live.clear();                              // another world: every pack is seen afresh
        return;
    }
    if (!g_dirty || now - g_written < 5.0) return;
    std::string text = encode_charge(g_saved);
    std::wstring temp = g_charge_path + L".tmp";
    if (!write_file(temp, text.data(), text.size()) || !MoveFileExW(temp.c_str(), g_charge_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        static std::atomic<int> logs{3};
        if (logs.fetch_sub(1) > 0) log(1, "could not write the Battery Pack charge file (error %lu)", GetLastError());
        g_written = now;
        return;
    }
    g_stamp = stamp_of(g_charge_path);
    g_written = now;
    g_dirty = false;
}

// ================================================================== the holder's tick (server)
using tick_t = void (*)(void* self, void* vehicle, void* scene);
anymaker::cell_hook h_tick;
void hk_tick(void* self, void* vehicle, void* scene) {
    auto original = reinterpret_cast<tick_t>(h_tick.original);
    if (!g_ready.load() || !self || !vehicle || !is_pack(self) || !can_read(static_cast<uint8_t*>(vehicle) + off::vehicle_id, 4))
        return original(self, vehicle, scene);
    double now = now_seconds();
    bool after_gap = now - g_last_tick > 2.0;    // no pack ticked for a while: a loading screen, maybe another world
    g_last_tick = now;
    refresh_settings();
    sync_charge(now, after_gap);
    const int32_t* extend = &at<int32_t>(self, off::component_extend);
    int64_t n = cells(extend[0], extend[1], extend[2]);
    double capacity = capacity_joules(n, g_settings);
    Key key{at<int32_t>(vehicle, off::vehicle_id), at<int32_t>(self, off::component_id)};
    double& energy = at<double>(self, off::battery_energy);
    double& cap = at<double>(self, off::battery_capacity);
    std::unique_lock lock(g_mutex);
    Live& live = g_live[self];
    if (live.pack.seen && (live.vehicle != key.vehicle || live.component != key.component)) live = Live{};   // a new part at a freed address
    bool first = !live.pack.seen;
    double dt = !first && now - live.last_tick < 1.0 ? now - live.last_tick : 0.0;
    live.vehicle = key.vehicle; live.component = key.component;
    auto saved = g_saved.find(key);
    Content c{energy, cap};
    double before = before_tick(live.pack, c, capacity, g_settings, saved != g_saved.end() ? &saved->second : nullptr);
    energy = c.energy; cap = c.capacity;
    bool damaged = at<bool>(self, off::battery_pending_damage);
    lock.unlock();
    original(self, vehicle, scene);
    lock.lock();
    Live& after = g_live[self];                      // the map may have been cleared meanwhile (another thread's sync)
    after.pack.seen = true; after.vehicle = key.vehicle; after.component = key.component;
    c.energy = energy;
    after_tick(after.pack, c, damaged, before, g_settings, dt);
    energy = c.energy;
    after.last_tick = now;
    double percent = charge_percent(c);
    if (double* slot = percent_slot(self)) *slot = percent;
    double& stored = g_saved[key];
    if (first || std::fabs(stored - c.energy) > std::max(1.0, c.capacity * 1e-4)) { stored = c.energy; g_dirty = true; }
    if (first || damaged) {
        static std::atomic<int> logs{20};
        if (logs.fetch_sub(1) > 0)
            log(0, "Battery Pack %d:%d: %lld cells (extend %d, %d, %d), %.1f kWh, charge %.0f%%%s", key.vehicle, key.component, (long long)n, extend[0], extend[1],
                extend[2], c.capacity / kJoulesPerKwh, percent, damaged ? " (crash damage)" : saved != g_saved.end() ? " (saved)" : "");
    }
}

// ================================================================== data outputs (server)
using get_f64_t = void (*)(double** ret, void* self, const anymaker::gc_string_view* name);
anymaker::cell_hook h_get_f64;
void hk_get_f64(double** ret, void* self, const anymaker::gc_string_view* name) {
    reinterpret_cast<get_f64_t>(h_get_f64.original)(ret, self, name);
    if (!ret || *ret || !self || !name || name->view() != "charge_percent" || !is_pack(self)) return;
    std::lock_guard lock(g_mutex);
    *ret = percent_slot(self);
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
        if (!matching_build()) { if (attempt == 0) log(1, "game build does not match the SDK reference; AnyBattery stays off"); return; }
        int roots = -1;
        void** add = route::cell(bind::definitions_add_definitions, &roots);
        auto ctor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_ctor));
        auto dtor = reinterpret_cast<file_ctor_t>(route::function(bind::definition_file_dtor));
        auto load = reinterpret_cast<file_load_t>(route::function(bind::definition_file_load));
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        void** tick = route::cell(bind::battery_tick);
        if (add && !route::add_cell_ok(add)) {
            static int logs = 3;
            if (logs-- > 0) log(1, "the definitions route led to %s, not _add_definitions; looking again", route::what_is(anymaker::readable(add, 8) ? *add : nullptr));
            add = nullptr;
        }
        if (load && !route::file_load_ok((void*)load)) load = nullptr;
        if (!add || !ctor || !dtor || !load || !sc || !tick) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (route root matches %d, add=%d file ctor=%d dtor=%d load=%d string=%d battery tick=%d)",
                    attempt == 179 ? "could not locate" : "still looking for", roots, !!add, !!ctor, !!dtor, !!load, !!sc, !!tick);
            continue;
        }
        g_file_ctor = ctor; g_file_dtor = dtor; g_file_load = load; g_string_ctor = sc;
        g_get_definition = reinterpret_cast<get_definition_t>(route::function(bind::definitions_get_by_id));
        if (!g_get_definition) { log(2, "definition lookup not found; AnyBattery is off"); return; }
        if (!write_files()) { log(2, "could not write the AnyBattery definition file; AnyBattery is off"); return; }
        init_charge_path();
        // The tick hook goes in before the part exists: a pack without it would hold nothing.
        if (!h_tick.install(tick, (void*)&hk_tick)) { log(2, "battery tick hook not installed (cell taken or not writable); AnyBattery is off"); return; }
        if (!h_add_definitions.install(add, (void*)&hk_add_definitions)) {
            log(2, "the definitions hook cell was already taken or not writable; AnyBattery is off");
            remove_hooks();
            return;
        }
        void** data = route::cell(bind::battery_get_data_f64);
        if (!data || !h_get_f64.install(data, (void*)&hk_get_f64)) log(1, "charge_percent output unavailable (energy_store and energy_capacity still work)");
        g_ready = true;
        log(0, "Ready after %d s. Load or start a world to add the Battery Pack (definition file %s).", attempt, g_json_path.c_str());
        return;
    }
}
}  // namespace
}  // namespace anybattery

using namespace anybattery;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    register_settings(AnyAPI_Services());
    std::thread(install_hooks).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
