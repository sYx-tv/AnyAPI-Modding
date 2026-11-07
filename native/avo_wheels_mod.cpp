// AnyVehicleOverhaul wheels: race wheels, drag slicks and drag front runners (Nate 2026-11-07). Ships as the
// "avowheels" part of the AnyVehicleOverhaul bundle; keep kModId and the avo_wheel_* item ids as they are, or players
// lose their tyres.
//
//   Race Wheel         1.5x the grip of a stock tyre, radius 0.33 m
//   Drag Slick         2.5x, radius 0.36 m, wide
//   Drag Front Runner  stock grip, skinny and light
// Each in three rim styles (avo_wheels_logic.h, wheel_mesh.h).
//
// How:
//   Items: the entries are made at start-up from the player's own rom/data/inventory_definitions.json (the 20 inch
//     car wheel is copied and rewritten, as AnyCareer does for Cash; no game data ships with the mod), written to
//     "AnyAPI and Modding/AnyWheels/wheels_items.json" and added to both scenes' item definitions right after the
//     game adds its own (inventory_definition_container._add_definitions). Meshes are generated into
//     rom/meshes/components.
//   Fit: the stock car wheel and tyre mount tunings (g_wheel_definition_wheel, _b, _c, _tyre_mount, _b) list the
//     tyres they take; ours are added to every list that takes the 20 inch wheel, and to the tyre size table
//     (g_tyre_data_container) so the wheel rolls at our radius. In place when the vector has room, else into a
//     larger copy (the game only reads these after start-up).
//   Grip: the game has no grip per tyre. The host's wheel tick (server_scene.vehicle_element.wheel.tick) runs with
//     the wheel's m_definition swapped for a copy whose sliding grip and friction-circle limit are scaled, and puts
//     the stock pointer back after. Only wheels wearing one of ours are touched. Co-op: the host's physics decides,
//     but every player needs the mod to have the items.
//
// EXPERIMENTAL. Hooks game functions through the experimental SDK (avo_wheels_bindings.h): Anymaker 0.2.0 / Steam
// build 26328453 only. First runs are diagnostic: read anymaker_modding.log.
#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_experimental.hpp"
#include "avo_wheels_bindings.h"
#include "route_cell.h"
#include "avo_wheels_logic.h"
#include "wheel_mesh.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace avowheels {
namespace {
constexpr const char* kModId = "avowheels";

AnyModHostV1 host;
std::atomic<bool> g_ready{false}, g_stop{false};

void log(int level, const char* fmt, ...) {
    if (!host.log) return;
    char buf[640];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    host.log(uint32_t(level), kModId, buf);
}
bool can_read(const void* p, size_t n) { return p && anymaker::readable(p, n); }
template <class T> T& at(void* base, ptrdiff_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }
template <class T> T at(const void* base, ptrdiff_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }
std::string utf8(const std::filesystem::path& p) { auto s = p.u8string(); return std::string(s.begin(), s.end()); }

namespace off {   // 0.2.0 reference layouts
constexpr ptrdiff_t wheel_definition = 8;               // server_scene.vehicle_element.wheel.m_definition
constexpr ptrdiff_t wheel_item = 152 + 8 + 24 + 8;      // .m_accessory (152) ._m_item_world (+8) ._m_element ref (+24), object pointer (+8)
constexpr ptrdiff_t item_definition = 144;              // server_scene.item_world: ptr<const inventory_definition>
constexpr ptrdiff_t definition_id = 0;                  // inventory_definition.m_id (string)
constexpr size_t tuning_size = 328;                     // vehicle_element_definition.wheel
constexpr ptrdiff_t tuning_tyres = 280;                 // m_compatible_tyre_definitions (vector<string>)
constexpr size_t tyre_data_size = 32;                   // tyre_data: string id, f64 radius (+16), f64 thickness (+24)
}

using string_ctor_t = void (*)(anymaker::gc_string_view*, const char*);
using file_ctor_t = void (*)(void* file);
using file_load_t = void (*)(bool* ret, void* file, const void* path);
using get_definition_t = void (*)(const uint8_t** ret, const void* container, const anymaker::gc_string_view* id);
using add_definitions_t = void (*)(void* container, const void* file, const void* outfit);
using tick_t = void (*)(void* wheel, void* vehicle, void* component, void* scene, const double* a, const double* b);
struct GamePath { int32_t store; uint32_t pad; anymaker::gc_string_view path; };
static_assert(sizeof(GamePath) == 24, "file.path");
constexpr int32_t kStoreSystem = 1;

string_ctor_t g_string_ctor;
file_ctor_t g_file_ctor, g_file_dtor;
file_load_t g_file_load;
get_definition_t g_get_definition;
anymaker::cell_hook h_add_definitions, h_tick;
thread_local bool t_adding = false;
std::string g_json;   // absolute, backslashes
const std::vector<Item>& items() { static const std::vector<Item> all = catalogue(); return all; }

// ------------------------------------------------------------------ files
bool write_file(const std::filesystem::path& p, const void* data, size_t n) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), std::streamsize(n));
    return bool(out);
}
std::string stock_wheel_entry() {
    std::ifstream in(std::filesystem::path(host.game_directory) / L"rom" / L"data" / L"inventory_definitions.json", std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        assetjson::Value root = assetjson::Parser(text).parse();
        std::string fallback;
        for (const auto& def : root.get("definitions").array) {
            if (def.kind != 'o' || def.get("class").text != "wheel") continue;
            const std::string& id = def.get("id").text;
            if (id == "wheel_rim_20") return text.substr(def.begin, def.end - def.begin);
            if (id == "wheel_car" && fallback.empty()) fallback = text.substr(def.begin, def.end - def.begin);
        }
        return fallback;
    } catch (const std::exception&) { return {}; }
}
bool write_files() {
    if (!host.game_directory || !host.plugin_directory) return false;
    const std::string stock = stock_wheel_entry();
    if (stock.empty()) { log(2, "no car wheel in the game's rom/data/inventory_definitions.json; the wheels are off"); return false; }
    const std::filesystem::path components = std::filesystem::path(host.game_directory) / L"rom" / L"meshes" / L"components";
    std::string entries;
    for (const auto& it : items()) {
        const std::string name = mesh_name(it.kind, it.style);
        for (int bare = 0; bare < 2; ++bare) {
            auto bytes = pdmesh::encode(wheelmesh::game_wheel(it.style, it.kind, !bare), name + (bare ? "_rim" : ""));
            if (!write_file(components / (name + (bare ? "_rim" : "") + ".mesh"), bytes.data(), bytes.size())) {
                log(2, "could not write %s.mesh into rom\\meshes\\components; the wheels are off", name.c_str());
                return false;
            }
        }
        std::string e = make_entry(stock, it, "meshes/components/" + name + ".mesh", "meshes/components/" + name + "_rim.mesh");
        if (e.empty()) { log(2, "could not rewrite the stock wheel entry for %s; the wheels are off", it.id.c_str()); return false; }
        entries += (entries.empty() ? "    " : ",\n    ") + e;
    }
    std::filesystem::path dir = std::filesystem::path(host.plugin_directory) / L"AnyWheels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string json = "{\n  \"definitions\": [\n" + entries + "\n  ]\n}\n";
    if (!write_file(dir / L"wheels_items.json", json.data(), json.size())) return false;
    g_json = utf8(dir / L"wheels_items.json");
    return true;
}

// ------------------------------------------------------------------ items into the game (both scenes)
bool add_ours(void* container, const void* outfit, const std::string& text) {
    static std::string kept[4];
    static std::atomic<int> next{0};
    std::string& chars = kept[next++ % 4];
    chars = text;
    GamePath path{kStoreSystem, 0, {}};
    g_string_ctor(&path.path, chars.c_str());
    alignas(16) uint8_t file[64]{};   // inventory_definition_file is 40 bytes
    g_file_ctor(file);
    bool ok = false;
    g_file_load(&ok, file, &path);
    if (ok) {
        t_adding = true;
        reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file, outfit);
        t_adding = false;
    }
    g_file_dtor(file);
    return ok;
}
bool has_definition(const void* container, const char* id) {
    anymaker::gc_string_view s{};
    g_string_ctor(&s, id);
    const uint8_t* found = nullptr;
    g_get_definition(&found, container, &s);
    return found != nullptr;
}
void hk_add_definitions(void* container, const void* file, const void* outfit) {
    reinterpret_cast<add_definitions_t>(h_add_definitions.original)(container, file, outfit);
    if (!g_ready || g_stop || t_adding || !container || has_definition(container, items().front().id.c_str())) return;
    std::string forward = g_json;
    for (char& c : forward) if (c == '\\') c = '/';
    bool ok = add_ours(container, outfit, g_json) || add_ours(container, outfit, forward);
    int found = 0;
    if (ok) for (const auto& it : items()) found += has_definition(container, it.id.c_str());
    static std::atomic<int> logs{6};
    if (logs.fetch_sub(1) > 0)
        log(found ? 0 : 2, "wheel items added=%d found %d of %d (%s)", ok ? 1 : 0, found, int(items().size()), g_json.c_str());
}

// ------------------------------------------------------------------ tyre lists and sizes
// A buffer the game can free. The game frees a vector's buffer with its aligned free (game.exe 0.2.0 +0x405208:
// HeapFree(crt heap, *(p & ~7) - 8)), so the block's real start sits in the 8 bytes before it, as its aligned malloc
// (+0x405224) lays it out. A plain HeapAlloc block there crashed the game with STATUS_HEAP_CORRUPTION when it cleared the
// vector (Nate's crash 2026-11-07 23:40, vector clear at game.exe+0x489a). The CRT heap is the process heap.
uint8_t* game_vector_alloc(size_t bytes) {
    constexpr size_t kAlign = 16;
    auto* raw = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + 8 + kAlign - 1));
    if (!raw) return nullptr;
    auto* p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(raw) + 8 + kAlign - 1) & ~uintptr_t(kAlign - 1));
    reinterpret_cast<void**>(p)[-1] = raw;
    return p;
}
// Appends `n` elements to a gc ring vector: in place while it has room, otherwise into a larger copy. `fill` writes
// element k into the slot it is given.
template <class Fill> bool append(anymaker::gc_vector_raw* v, int n, Fill fill) {
    if (!can_read(v, sizeof *v) || v->count < 0 || v->capacity < 0 || v->count > v->capacity || v->element_size == 0 || v->element_size > 256) return false;
    const uint32_t es = v->element_size;
    if (v->capacity - v->count < n) {
        const int cap = v->count + n + 4;
        auto* buf = game_vector_alloc(size_t(cap) * es);   // the old buffer is left alone (the game may still be reading it)
        if (!buf) return false;
        for (int32_t i = 0; i < v->count; ++i)
            std::memcpy(buf + size_t(i) * es, v->buffer + size_t((int64_t(v->offset) + i) % v->capacity) * es, es);
        // Publish the new buffer before the larger capacity, so a reader never sees room it does not have.
        v->offset = 0;
        v->buffer = buf;
        v->capacity = cap;
    }
    for (int k = 0; k < n; ++k) {
        uint8_t* slot = v->buffer + size_t((int64_t(v->offset) + v->count) % v->capacity) * es;
        std::memset(slot, 0, es);
        fill(k, slot);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&v->count));
    }
    return true;
}
bool lists(const anymaker::gc_vector_raw* v, std::string_view id) {
    if (!can_read(v, sizeof *v) || v->element_size != sizeof(anymaker::gc_string_view) || v->count <= 0 || v->count > v->capacity || !can_read(v->buffer, size_t(v->capacity) * 16)) return false;
    for (int32_t i = 0; i < v->count; ++i) {
        auto* s = reinterpret_cast<const anymaker::gc_string_view*>(v->buffer + size_t((int64_t(v->offset) + i) % v->capacity) * 16);
        if (s->length > 0 && can_read(s->data, size_t(s->length)) && s->view() == id) return true;
    }
    return false;
}
void fit_tyres() {
    const anymaker::global_desc* tunings[] = {&bind::g_wheel_definition_wheel, &bind::g_wheel_definition_wheel_b, &bind::g_wheel_definition_wheel_c,
                                              &bind::g_wheel_definition_tyre_mount, &bind::g_wheel_definition_tyre_mount_b};
    const int n = int(items().size());
    int patched = 0, skipped = 0;
    for (const auto* g : tunings) {
        auto* tuning = static_cast<uint8_t*>(anymaker::experimental::global(*g));
        if (!can_read(tuning, off::tuning_size)) { ++skipped; continue; }
        auto* v = reinterpret_cast<anymaker::gc_vector_raw*>(tuning + off::tuning_tyres);
        if (!lists(v, "wheel_rim_20") && !lists(v, "wheel_car")) { ++skipped; continue; }
        if (lists(v, items().front().id)) { ++patched; continue; }
        if (append(v, n, [&](int k, uint8_t* slot) { g_string_ctor(reinterpret_cast<anymaker::gc_string_view*>(slot), items()[size_t(k)].id.c_str()); })) ++patched;
        else ++skipped;
    }
    auto* sizes = static_cast<anymaker::gc_vector_raw*>(anymaker::experimental::global(bind::g_tyre_data_container));
    bool sized = can_read(sizes, sizeof *sizes) && sizes->element_size == off::tyre_data_size &&
                 append(sizes, n, [&](int k, uint8_t* slot) {
                     const Item& it = items()[size_t(k)];
                     const Tyre t = tyre(it.kind);
                     g_string_ctor(reinterpret_cast<anymaker::gc_string_view*>(slot), it.id.c_str());
                     at<double>(slot, 16) = t.radius;
                     at<double>(slot, 24) = t.width;
                 });
    log(patched && sized ? 0 : 2, "wheel tyre lists patched %d (left %d), tyre size table %s", patched, skipped, sized ? "extended" : "NOT readable");
}

// ------------------------------------------------------------------ grip (host wheel tick)
// A grippier copy of a stock tuning per (tuning, grip). Copies live for the whole run.
struct Copy { const uint8_t* stock; double grip; uint8_t* copy; };
std::mutex g_copies_mutex;
std::vector<Copy> g_copies;
std::unordered_map<const void*, double> g_grip_by_definition;   // inventory_definition -> grip (0 = not ours)
uint8_t* grip_copy(const uint8_t* stock, double grip) {
    std::lock_guard lock(g_copies_mutex);
    for (const auto& c : g_copies) if (c.stock == stock && c.grip == grip) return c.copy;
    if (g_copies.size() > 64 || !can_read(stock, off::tuning_size)) return nullptr;
    Grip base{at<double>(stock, kGripFriction), at<double>(stock, kFrictionCircleMu)};
    if (!(base.grip_friction > 0 && base.grip_friction < 100 && base.circle_mu > 0 && base.circle_mu < 100)) {
        log(1, "wheel tuning %p has unexpected grip values (%.3f, %.3f); its tyres keep stock grip", stock, base.grip_friction, base.circle_mu);
        g_copies.push_back({stock, grip, nullptr});
        return nullptr;
    }
    auto* copy = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, off::tuning_size));
    if (copy) {
        std::memcpy(copy, stock, off::tuning_size);
        Grip g = scaled(base, grip);
        at<double>(copy, kGripFriction) = g.grip_friction;
        at<double>(copy, kFrictionCircleMu) = g.circle_mu;
        log(0, "grip x%.1f tuning made from %p: grip %.2f -> %.2f, friction circle %.2f -> %.2f", grip, stock, base.grip_friction, g.grip_friction, base.circle_mu, g.circle_mu);
    }
    g_copies.push_back({stock, grip, copy});
    return copy;
}
bool is_copy(const void* p) {
    std::lock_guard lock(g_copies_mutex);
    for (const auto& c : g_copies) if (c.copy == p) return true;
    return false;
}
double grip_of_wheel(const uint8_t* wheel) {
    if (!can_read(wheel + off::wheel_item, 8)) return 0;
    const uint8_t* item = at<const uint8_t*>(wheel, off::wheel_item);
    if (!item || !can_read(item + off::item_definition, 8)) return 0;
    const void* def = at<const void*>(item, off::item_definition);
    if (!def) return 0;
    {
        std::lock_guard lock(g_copies_mutex);
        if (auto f = g_grip_by_definition.find(def); f != g_grip_by_definition.end()) return f->second;
    }
    double grip = 0;
    if (can_read(def, 16)) {
        auto* id = static_cast<const anymaker::gc_string_view*>(def);
        if (id->length > 0 && id->length < 128 && can_read(id->data, size_t(id->length))) {
            grip = grip_of(id->view());
            static std::atomic<int> logs{8};
            if (logs.fetch_sub(1) > 0) log(0, "wheel tyre seen: %.*s (grip x%.1f)", int(id->length), id->data, grip ? grip : 1.0);
        }
    }
    std::lock_guard lock(g_copies_mutex);
    if (g_grip_by_definition.size() > 4096) g_grip_by_definition.clear();
    g_grip_by_definition[def] = grip;
    return grip;
}
void hk_tick(void* wheel, void* vehicle, void* component, void* scene, const double* a, const double* b) {
    auto original = reinterpret_cast<tick_t>(h_tick.original);
    auto* w = static_cast<uint8_t*>(wheel);
    if (!g_ready || !w || !can_read(w, off::wheel_item + 8)) return original(wheel, vehicle, component, scene, a, b);
    uint8_t*& slot = at<uint8_t*>(w, off::wheel_definition);
    uint8_t* stock = slot;
    if (stock && is_copy(stock)) return original(wheel, vehicle, component, scene, a, b);   // never stack copies
    const double grip = grip_of_wheel(w);
    uint8_t* copy = grip > 0 && grip != 1.0 && stock ? grip_copy(stock, grip) : nullptr;
    if (!copy) return original(wheel, vehicle, component, scene, a, b);
    slot = copy;
    original(wheel, vehicle, component, scene, a, b);
    if (slot == copy) slot = stock;   // the tick may have set a new definition; keep that one
}

// ------------------------------------------------------------------ install (background thread)
void install() {
    using namespace anymaker::experimental;
    for (int attempt = 0; attempt < 180 && !g_stop; ++attempt) {
        if (attempt) Sleep(1000);
        if (!matching_build()) { if (!attempt) log(1, "game build does not match the SDK reference; the wheels stay off"); return; }
        auto f = [](const anymaker::func_desc& d) { return route::function(d); };
        void* fns[] = {f(bind::inv_file_ctor), f(bind::inv_file_dtor), f(bind::inv_file_load), f(bind::inv_get_definition_by_id)};
        void** add = route::cell(bind::inv_add_definitions);
        void** tick = route::cell(bind::server_wheel_tick);
        auto sc = reinterpret_cast<string_ctor_t>(native(anymaker::sym::string_ctor_cstr_rva));
        bool all = add && tick && sc;
        for (void* p : fns) all = all && p;
        bool add_ok = add && anymaker::readable(add, 8) && (route::code_is(*add, bind::inv_add_definitions.code_sig) || route::in_other_module(*add));
        bool distinct = fns[0] != fns[1] && fns[0] != fns[2] && fns[1] != fns[2] && (!add || (*add != fns[0] && *add != fns[2]));
        if (!all || !add_ok || !distinct) {
            if (attempt == 20 || attempt == 179)
                log(attempt == 179 ? 2 : 1, "%s the game functions (add %d ok %d, tick %d, file %d/%d/%d, lookup %d, string %d, distinct %d)",
                    attempt == 179 ? "could not locate" : "still looking for", !!add, add_ok, !!tick, !!fns[0], !!fns[1], !!fns[2], !!fns[3], !!sc, distinct);
            continue;
        }
        g_file_ctor = (file_ctor_t)fns[0]; g_file_dtor = (file_ctor_t)fns[1]; g_file_load = (file_load_t)fns[2];
        g_get_definition = (get_definition_t)fns[3]; g_string_ctor = sc;
        if (!write_files()) { log(2, "could not write the wheel files; the wheels are off"); return; }
        fit_tyres();
        if (!h_add_definitions.install(add, (void*)&hk_add_definitions)) { log(2, "item hook not installed; the wheels are off"); return; }
        if (!h_tick.install(tick, (void*)&hk_tick)) log(2, "wheel tick hook not installed; the new tyres grip like stock ones");
        g_ready = true;
        log(0, "Ready after %d s: %d wheels (%s). Load or start a world to add them.", attempt, int(items().size()), g_json.c_str());
        return;
    }
}
void remove_hooks() {
    h_tick.remove((void*)&hk_tick);
    h_add_definitions.remove((void*)&hk_add_definitions);
}
}  // namespace
}  // namespace avowheels

using namespace avowheels;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h, AnyModCallbacksV1* out) {
    if (!h || !out || h->struct_size != sizeof(*h) || h->abi != ANYAPI_MOD_ABI || out->struct_size != sizeof(*out)) return false;
    host = *h; out->id = kModId;
    out->shutdown = [](void*) { g_stop = true; remove_hooks(); };
    return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady() {
    std::thread(install).detach();   // resolving scans game memory; keep it off the loader and frame threads
}
