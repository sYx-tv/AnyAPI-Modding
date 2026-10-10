#pragma once
// Read-only walks over the client vehicle graph, shared by AnyMirror and AnyLights.
// Offsets come from the experimental SDK reference layouts (metadata, Anymaker 0.1.24 / Steam build
// 25826614). Call these only on the game's main thread with pointers the game handed you this tick.
#include "anymaker_sdk_runtime.hpp"
#include <cstdint>
#include <cstring>

namespace vehicle_reads {
template <class T> inline T rd(const uint8_t* p, ptrdiff_t o) { T v; std::memcpy(&v, p + o, sizeof v); return v; }
template <class T> inline void wr(uint8_t* p, ptrdiff_t o, T v) { std::memcpy(p + o, &v, sizeof v); }

namespace off {
// client_scene
constexpr ptrdiff_t scene_vehicles = 160 + 8;        // m_vehicles (container) -> _m_elements vector<ref<vehicle>>
constexpr ptrdiff_t scene_overlay_ui = 10088;        // m_overlay_ui (embedded)
// client_scene.vehicle
constexpr ptrdiff_t vehicle_id = 8;                  // s32
constexpr ptrdiff_t vehicle_grids = 16 + 8;          // m_grids -> vector<ref<vehicle_grid>>
constexpr ptrdiff_t vehicle_nodes = 72 + 8;          // m_nodes -> vector<ref<vehicle_node>>
constexpr ptrdiff_t vehicle_edges = 128 + 8;         // m_edges -> vector<ref<vehicle_edge>>
constexpr ptrdiff_t vehicle_plates = 184 + 8;        // m_plates -> vector<ref<vehicle_plate>>
// client_scene.vehicle_grid
constexpr ptrdiff_t grid_components = 40 + 8;        // m_components -> vector<ref<vehicle_component>>
// client_scene.vehicle_node
constexpr ptrdiff_t node_grid_pos = 16 + 8;          // property_vec3_s32 _m_value
// client_scene.vehicle_component
constexpr ptrdiff_t component_id = 8;                // s32
constexpr ptrdiff_t component_definition = 176;      // ptr<const vehicle_component_definition>
constexpr ptrdiff_t component_parent_vehicle = 200;  // ptr<client_scene.vehicle>
// vehicle_component_definition
constexpr ptrdiff_t def_id = 0, def_display_name = 16, def_class = 48, def_category = 440;
constexpr ptrdiff_t def_alternator_wattage = 808, def_motor_wattage = 1432;
}  // namespace off

struct Vec3i { int32_t x, y, z; };
inline bool operator==(const Vec3i& a, const Vec3i& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// Visits the object pointer of every ref<T> in a game vector. Returns false when the vector looks wrong.
template <class F> bool for_each_ref(const uint8_t* vector_address, int max_count, F&& visit) {
    if (!anymaker::readable(vector_address, sizeof(anymaker::gc_vector_raw))) return false;
    anymaker::gc_vector_raw v;
    std::memcpy(&v, vector_address, sizeof v);
    if (v.count == 0) return true;
    if (v.element_size != 16 || v.count < 0 || v.count > max_count || v.capacity < v.count || v.offset < 0 ||
        v.offset >= v.capacity || !v.buffer || !anymaker::readable(v.buffer, size_t(v.capacity) * 16)) return false;
    for (int32_t i = 0; i < v.count; ++i) {
        auto* ref = v.at<anymaker::gc_ref_raw<uint8_t>>(i);
        if (ref && ref->ptr && !visit(ref->ptr)) break;
    }
    return true;
}

inline uint8_t* find_vehicle(const uint8_t* scene, int32_t id) {
    if (!scene || id <= 0) return nullptr;
    uint8_t* found = nullptr;
    for_each_ref(scene + off::scene_vehicles, 65536, [&](uint8_t* vehicle) {
        if (anymaker::readable(vehicle, 2400) && rd<int32_t>(vehicle, off::vehicle_id) == id) { found = vehicle; return false; }
        return true;
    });
    return found;
}

// Grid bounds of every node in a vehicle (the points edges connect). False when the vehicle has none.
inline bool node_bounds(const uint8_t* vehicle, Vec3i& lo, Vec3i& hi, int* count = nullptr) {
    bool any = false; int n = 0;
    for_each_ref(vehicle + off::vehicle_nodes, 1 << 20, [&](uint8_t* node) {
        if (!anymaker::readable(node, 40)) return true;
        Vec3i p = rd<Vec3i>(node, off::node_grid_pos);
        if (!any) { lo = hi = p; any = true; }
        lo = {p.x < lo.x ? p.x : lo.x, p.y < lo.y ? p.y : lo.y, p.z < lo.z ? p.z : lo.z};
        hi = {p.x > hi.x ? p.x : hi.x, p.y > hi.y ? p.y : hi.y, p.z > hi.z ? p.z : hi.z};
        ++n;
        return true;
    });
    if (count) *count = n;
    return any;
}

// Visits every component of a vehicle across all of its grids.
template <class F> void for_each_component(const uint8_t* vehicle, F&& visit) {
    for_each_ref(vehicle + off::vehicle_grids, 4096, [&](uint8_t* grid) {
        if (anymaker::readable(grid, 112)) for_each_ref(grid + off::grid_components, 1 << 20, [&](uint8_t* component) {
            if (anymaker::readable(component, 224)) visit(component);
            return true;
        });
        return true;
    });
}

inline int32_t vector_count(const uint8_t* vector_address) {
    if (!anymaker::readable(vector_address, sizeof(anymaker::gc_vector_raw))) return -1;
    anymaker::gc_vector_raw v; std::memcpy(&v, vector_address, sizeof v);
    return v.count >= 0 && v.count <= v.capacity ? v.count : -1;
}

// Copies a game string into a NUL-terminated buffer; empty when unreadable.
inline void copy_string(const uint8_t* string_address, char* out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (!anymaker::readable(string_address, 16)) return;
    auto s = rd<anymaker::gc_string_view>(string_address, 0);
    if (!s.data || s.length <= 0 || s.length > 4096 || !anymaker::readable(s.data, size_t(s.length))) return;
    size_t n = size_t(s.length) < cap - 1 ? size_t(s.length) : cap - 1;
    std::memcpy(out, s.data, n);
    out[n] = 0;
}
}  // namespace vehicle_reads
