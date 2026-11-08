#pragma once
// Compact engines in the game (compact_engine_logic.h): included once by turbo_mod.cpp inside namespace turbo, after
// the liquid helpers (create_oil_side's), so it shares their game functions, g_mutex and the engine tick hook.
//
// What the stock engine does when an oil or coolant manifold snaps on (engine.add_mating_component, game 0.1.27
// decompile, unchanged in 0.2.0 by layout): it joins the manifold's liquid node to its own oil content
// (m_content_lubricant) or coolant content (m_content_coolant) through the vehicle's liquid node container, and counts
// the manifold (m_manifold_count_lubricant / _coolant). A compact engine does the same with liquid nodes of its own,
// bound to its "compact_oil_N" / "compact_coolant_N" logic nodes. Fuel: the engine's tick reads each of its fuel
// manifolds' fuel content (+0x230) and writes their active factor (+0x320), nothing else of them; the fuel manifold's own
// tick ticks that content and holds its node at static pressure -6 (it draws). A compact engine gets one block shaped
// like a fuel manifold, with a fuel content bound to its "compact_fuel_0" node, in its fuel manifold list; this code
// does that manifold's tick each engine tick.
//   Proven: the stock engine joins manifold nodes this way (decompile). Likely: the same works for nodes the engine
//   owns. Unproven in game: all of it (Nate's first test).

namespace off {
constexpr ptrdiff_t engine_content_lubricant = 0x2a0, engine_content_coolant = 0x388;   // vehicle_liquid_content_volume
constexpr ptrdiff_t engine_fuel_manifolds = 0x510;   // vector<ptr<fuel_manifold>>
constexpr ptrdiff_t engine_count_coolant = 0x5a0, engine_count_lubricant = 0x5a4;   // s32
constexpr ptrdiff_t definition_logic_nodes = 184;     // vehicle_component_definition.m_logic_nodes (vector<logic_node>, id string at +0)
constexpr size_t fuel_manifold_size = 0x328;
constexpr ptrdiff_t fuel_content = 0x230, fuel_mated = 0x318;
}  // namespace off

using static_pressure_t = void (*)(void* node, const double* pressure);
static_pressure_t g_set_static_pressure;
using engine_life_t = void (*)(void* self, void* vehicle);
using engine_destroy_t = void (*)(void* self, void* vehicle, void* scene);
anymaker::cell_hook h_compact_create, h_compact_destroy;

struct CompactSide {
    void* node[8]{};
    int kind[8]{};
    int count = 0;
    uint8_t* fuel = nullptr;     // the fuel manifold-shaped block (ours)
    void* fuel_node = nullptr;
};
std::unordered_map<void*, CompactSide*> g_compact;   // engine -> its ports (under g_mutex)

// A vector buffer the game can free (its aligned free reads the block start 8 bytes before; see avo_wheels_mod.cpp).
uint8_t* compact_vector_alloc(size_t bytes) {
    auto* raw = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + 8 + 15));
    if (!raw) return nullptr;
    auto* p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(raw) + 8 + 15) & ~uintptr_t(15));
    reinterpret_cast<void**>(p)[-1] = raw;
    return p;
}
anymaker::gc_vector_raw* fuel_list(void* engine) { return reinterpret_cast<anymaker::gc_vector_raw*>(static_cast<uint8_t*>(engine) + off::engine_fuel_manifolds); }
bool list_ok(const anymaker::gc_vector_raw* v) {
    return anymaker::readable(v, sizeof *v) && v->element_size == 8 && v->count >= 0 && v->capacity >= v->count && v->count < 4096 &&
           (!v->capacity || anymaker::readable(v->buffer, size_t(v->capacity) * 8));
}
void** list_slot(anymaker::gc_vector_raw* v, int32_t i) { return reinterpret_cast<void**>(v->buffer + size_t((int64_t(v->offset) + i) % v->capacity) * 8); }
bool list_has(anymaker::gc_vector_raw* v, const void* p) {
    for (int32_t i = 0; i < v->count; ++i) if (*list_slot(v, i) == p) return true;
    return false;
}
bool list_push(anymaker::gc_vector_raw* v, void* p) {
    if (v->count == v->capacity) {
        const int32_t cap = v->count + 4;
        uint8_t* buf = compact_vector_alloc(size_t(cap) * 8);
        if (!buf) return false;
        for (int32_t i = 0; i < v->count; ++i) std::memcpy(buf + size_t(i) * 8, list_slot(v, i), 8);
        v->offset = 0; v->buffer = buf; v->capacity = cap;   // the old buffer is left (the game may still hold it)
    }
    *list_slot(v, v->count) = p;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&v->count));
    return true;
}
void list_remove(anymaker::gc_vector_raw* v, const void* p) {
    for (int32_t i = 0; i < v->count; ++i) {
        if (*list_slot(v, i) != p) continue;
        for (int32_t j = i; j + 1 < v->count; ++j) *list_slot(v, j) = *list_slot(v, j + 1);
        InterlockedDecrement(reinterpret_cast<volatile LONG*>(&v->count));
        return;
    }
}

void compact_build(void* engine, void* vehicle) {
    size_t length = 0;
    const char* id = definition_id_of(engine, length);
    if (!id || !compact::is_compact_id(std::string_view(id, length))) return;
    static std::atomic<int> logs{12};
    if (!g_oil_ready || !g_set_static_pressure) {
        if (logs.fetch_sub(1) > 0) log(1, "compact engine: liquid functions not found (oil %d, pressure %d); its built-in ports do nothing, use manifolds", g_oil_ready, !!g_set_static_pressure);
        return;
    }
    const void* definition = at<const void*>(engine, off::component_definition);
    auto* nodes = reinterpret_cast<const anymaker::gc_vector_raw*>(static_cast<const uint8_t*>(definition) + off::definition_logic_nodes);
    void* container = liquid_container_of(vehicle);
    auto create_node = reinterpret_cast<create_liquid_node_t>(container_method(container, off::container_create_node_slot));
    void* grid = anymaker::readable(static_cast<uint8_t*>(engine) + off::component_grid, 8) ? at<void*>(engine, off::component_grid) : nullptr;
    if (!anymaker::readable(nodes, sizeof *nodes) || nodes->count <= 0 || nodes->count > 64 || nodes->element_size < 16 || nodes->element_size > 128 ||
        !anymaker::readable(nodes->buffer, size_t(nodes->capacity) * nodes->element_size) || !create_node || !grid ||
        !anymaker::readable(static_cast<uint8_t*>(grid) + off::grid_origin_dir, 24)) {
        if (logs.fetch_sub(1) > 0) log(1, "compact engine %.*s: its logic nodes or the liquid container are not readable; no built-in ports", int(length), id);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_compact.count(engine)) return;
    auto* side = new CompactSide();
    int found[3] = {0, 0, 0};
    for (int32_t i = 0; i < nodes->count && side->count < 8; ++i) {
        auto* s = reinterpret_cast<const anymaker::gc_string_view*>(nodes->buffer + size_t((int64_t(nodes->offset) + i) % nodes->capacity) * nodes->element_size);
        if (s->length <= 0 || s->length > 32 || !anymaker::readable(s->data, size_t(s->length))) continue;
        int kind = compact::kind_of_node(s->view());
        if (kind < 0) continue;
        void* content = nullptr;
        if (kind == compact::kOil) content = static_cast<uint8_t*>(engine) + off::engine_content_lubricant;
        else if (kind == compact::kCoolant) content = static_cast<uint8_t*>(engine) + off::engine_content_coolant;
        else {
            if (side->fuel) continue;
            side->fuel = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, off::fuel_manifold_size));
            if (!side->fuel) continue;
            g_liquid_ctor(side->fuel + off::fuel_content);
            int32_t layer = 0; double factor = 0.05;   // about a manifold's worth of fuel in a big part
            g_liquid_create(side->fuel + off::fuel_content, vehicle, engine, static_cast<uint8_t*>(grid) + off::grid_origin_dir, &layer, &factor);
            at<void*>(side->fuel, off::fuel_mated) = engine;
            content = side->fuel + off::fuel_content;
        }
        void* node = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, off::liquid_node_size);
        if (!node) continue;
        g_liquid_node_ctor(node);
        void* flood = nullptr; int32_t index = i; bool expandable = false;
        create_node(container, node, engine, &flood, &index, &content, &expandable);
        side->node[side->count] = node; side->kind[side->count] = kind; ++side->count;
        if (kind == compact::kOil) ++at<int32_t>(engine, off::engine_count_lubricant);
        else if (kind == compact::kCoolant) ++at<int32_t>(engine, off::engine_count_coolant);
        else side->fuel_node = node;
        ++found[kind];
    }
    g_compact[engine] = side;
    if (logs.fetch_sub(1) > 0)
        log(0, "compact engine %.*s ready: %d oil, %d coolant, %d fuel ports built in (node indices bound: %d)", int(length), id, found[0], found[1], found[2],
            [&] { int b = 0; for (int k = 0; k < side->count; ++k) b += at<int32_t>(side->node[k], off::node_node_index) >= 0; return b; }());
}

// The fuel port's tick (a fuel manifold's), and the engine's fuel list keeps our block (the game may rebuild it).
void compact_tick(void* engine, void* vehicle) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it = g_compact.find(engine);
    if (it == g_compact.end() || !it->second->fuel) return;
    CompactSide* side = it->second;
    double still = 0, draw = -6.0;
    g_liquid_tick(side->fuel + off::fuel_content, vehicle, &still);
    if (side->fuel_node) g_set_static_pressure(side->fuel_node, &draw);
    anymaker::gc_vector_raw* list = fuel_list(engine);
    if (list_ok(list) && !list_has(list, side->fuel)) list_push(list, side->fuel);
}

void compact_drop(void* engine, void* vehicle) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it = g_compact.find(engine);
    if (it == g_compact.end()) return;
    CompactSide* side = it->second;
    g_compact.erase(it);
    if (side->fuel) { anymaker::gc_vector_raw* list = fuel_list(engine); if (list_ok(list)) list_remove(list, side->fuel); }
    void* container = vehicle ? liquid_container_of(vehicle) : nullptr;
    auto destroy_node = reinterpret_cast<destroy_node_t>(container_method(container, off::container_destroy_node_slot));
    if (!destroy_node) { log(2, "liquid node container not readable at destroy; leaking a compact engine's ports rather than risk a crash"); return; }
    for (int k = side->count - 1; k >= 0; --k) {
        destroy_node(container, side->node[k]);
        g_liquid_node_dtor(side->node[k]);
        HeapFree(GetProcessHeap(), 0, side->node[k]);
        if (side->kind[k] == compact::kOil) --at<int32_t>(engine, off::engine_count_lubricant);
        else if (side->kind[k] == compact::kCoolant) --at<int32_t>(engine, off::engine_count_coolant);
    }
    if (side->fuel) { g_liquid_dtor(side->fuel + off::fuel_content); HeapFree(GetProcessHeap(), 0, side->fuel); }
    delete side;
}

void hk_compact_create(void* self, void* vehicle) {
    reinterpret_cast<engine_life_t>(h_compact_create.original)(self, vehicle);
    if (self && vehicle) compact_build(self, vehicle);
}
void hk_compact_destroy(void* self, void* vehicle, void* scene) {
    if (self) compact_drop(self, vehicle);
    reinterpret_cast<engine_destroy_t>(h_compact_destroy.original)(self, vehicle, scene);
}

void install_compact() {
    using namespace anymaker::experimental;
    g_set_static_pressure = reinterpret_cast<static_pressure_t>(route::function(bind::liquid_node_set_static_pressure));
    void** create = route::cell(bind::engine_create);
    void** destroy = route::cell(bind::engine_destroy);
    if (!g_oil_ready || !g_set_static_pressure || !create || !destroy) {
        log(1, "compact engines' built-in ports off (liquid %d, pressure %d, create %d, destroy %d); they work as stock engines with manifolds", g_oil_ready,
            !!g_set_static_pressure, !!create, !!destroy);
        return;
    }
    if (!h_compact_destroy.install(destroy, (void*)&hk_compact_destroy)) { log(1, "compact engines: engine destroy hook not installed; built-in ports off"); return; }
    if (!h_compact_create.install(create, (void*)&hk_compact_create)) {
        h_compact_destroy.remove((void*)&hk_compact_destroy);
        log(1, "compact engines: engine create hook not installed; built-in ports off");
        return;
    }
    log(0, "compact engines: built-in oil, coolant and fuel ports on");
}
void remove_compact() {
    h_compact_create.remove((void*)&hk_compact_create);
    h_compact_destroy.remove((void*)&hk_compact_destroy);
}
