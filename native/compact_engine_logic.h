#pragma once
// Compact engines (Nate 2026-11-08: "the engines take up a lot of space ... after all the manifolds"): the nine stock
// engines with their oil, coolant and fuel ports built into the block, so no oil, coolant or fuel manifold has to hang
// off them. Pure logic, tested in tests/compact_engine_logic_test.cpp.
//
// Each one is the stock engine's own definition (read from the player's rom at start-up, as the built engines are)
// with a new id ("compact_" + the stock id; never rename), name, description, five liquid logic nodes and its own mesh
// tiles (the stock tiles with port markers on the front tile). The stock engine joins an oil or coolant manifold's
// liquid node to its own oil or coolant (engine.add_mating_component); a compact engine joins its own liquid nodes to
// them instead (turbo_mod.cpp, compact::). Fuel the stock engine draws from its fuel manifolds' contents; a compact
// engine gets one fuel content of its own, fed by its fuel port, in the same list.
//
// Where the ports go, on the front cell row (z = the zones' lowest z: the front tile, which a stretched engine never
// moves): oil x2 on the bottom face at the left and right columns, coolant x2 on the left face (x-) one and two cells
// above the bottom, fuel on the right face (x+) one cell above the bottom. A cell an existing logic node already uses
// facing the same way moves one cell back along z.
#include "built_engine_logic.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace compact {

constexpr const char* kPrefix = "compact_";
constexpr size_t kPrefixLength = 8;
enum Kind { kOil = 0, kCoolant = 1, kFuel = 2 };
inline const char* kind_key(int k) { return k == kOil ? "oil" : k == kCoolant ? "coolant" : "fuel"; }
// Surface directions (e_vehicle_component_surface_dir): 0 x-, 1 x+, 2 y-, 3 y+, 4 z-, 5 z+.
enum Dir { kXNeg = 0, kXPos = 1, kYNeg = 2, kYPos = 3, kZNeg = 4, kZPos = 5 };

struct Port { int kind, index; int pos[3]; int dir; };
inline std::string port_id(const Port& p) { return std::string("compact_") + kind_key(p.kind) + "_" + std::to_string(p.index); }

// Our logic node ids: "compact_oil_0" -> kind kOil (and its index); -1 when not one of ours.
inline int kind_of_node(std::string_view id, int* index = nullptr) {
    if (id.size() < 10 || id.compare(0, 8, "compact_") != 0) return -1;
    std::string_view rest = id.substr(8);
    for (int k = 0; k < 3; ++k) {
        std::string key = std::string(kind_key(k)) + "_";
        if (rest.size() == key.size() + 1 && rest.compare(0, key.size(), key) == 0 && rest.back() >= '0' && rest.back() <= '9') {
            if (index) *index = rest.back() - '0';
            return k;
        }
    }
    return -1;
}

inline bool is_compact_id(std::string_view id) {
    return id.size() > kPrefixLength && id.compare(0, kPrefixLength, kPrefix) == 0 && built::is_stock_engine(id.substr(kPrefixLength)) &&
           id.substr(kPrefixLength) != "rotary_engine";
}

// ------------------------------------------------------------------------------------- reading the stock entry
// Integers in a JSON array "[a, b, c]".
inline bool ints3(std::string_view v, int out[3]) {
    size_t i = built::skip_ws(v, 0);
    if (i >= v.size() || v[i] != '[') return false;
    ++i;
    for (int k = 0; k < 3; ++k) {
        i = built::skip_ws(v, i);
        bool neg = i < v.size() && v[i] == '-';
        if (neg) ++i;
        if (i >= v.size() || v[i] < '0' || v[i] > '9') return false;
        long n = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') { n = n * 10 + (v[i] - '0'); if (n > 1000) return false; ++i; }
        out[k] = int(neg ? -n : n);
        i = built::skip_ws(v, i);
        if (k < 2) { if (i >= v.size() || v[i] != ',') return false; ++i; }
    }
    return true;
}
inline std::string_view value(std::string_view o, const built::Member& m) { return o.substr(m.value_begin, m.value_end - m.value_begin); }
inline std::string_view find(std::string_view o, std::string_view key) {
    for (const built::Member& m : built::members(o)) if (m.key == key) return value(o, m);
    return {};
}
// The objects of a JSON array's text.
inline std::vector<std::string_view> objects(std::string_view a) {
    std::vector<std::string_view> out;
    size_t i = built::skip_ws(a, 0);
    if (i >= a.size() || a[i] != '[') return out;
    i = built::skip_ws(a, i + 1);
    while (i < a.size() && a[i] != ']') {
        size_t e = built::value_end(a, i);
        if (e == std::string_view::npos) return {};
        if (a[i] == '{') out.push_back(a.substr(i, e - i));
        i = built::skip_ws(a, e);
        if (i < a.size() && a[i] == ',') i = built::skip_ws(a, i + 1);
    }
    return out;
}
struct Box { int lo[3] = {INT_MAX, INT_MAX, INT_MAX}, hi[3] = {INT_MIN, INT_MIN, INT_MIN}; bool ok() const { return lo[0] <= hi[0]; } };
inline Box bounds_of(std::string_view entry) {
    Box b;
    for (std::string_view z : objects(find(entry, "zones"))) {
        int lo[3], hi[3];
        if (!ints3(find(z, "bounds_min"), lo) || !ints3(find(z, "bounds_max"), hi)) continue;
        for (int k = 0; k < 3; ++k) { b.lo[k] = std::min(b.lo[k], std::min(lo[k], hi[k])); b.hi[k] = std::max(b.hi[k], std::max(lo[k], hi[k])); }
    }
    return b;
}
struct Used { int pos[3]; int dir; };
inline std::vector<Used> used_nodes(std::string_view entry) {
    std::vector<Used> out;
    for (std::string_view n : objects(find(entry, "logic_nodes"))) {
        Used u{{0, 0, 0}, 0};
        ints3(find(n, "pos"), u.pos);
        std::string_view d = find(n, "direction");
        if (!d.empty() && d[0] >= '0' && d[0] <= '5') u.dir = d[0] - '0';
        out.push_back(u);
    }
    return out;
}

// The five ports for an engine with these bounds and logic nodes; empty when the bounds are unreadable.
inline std::vector<Port> ports_for(const Box& b, const std::vector<Used>& used) {
    if (!b.ok()) return {};
    const int x0 = b.lo[0], x1 = b.hi[0], y0 = b.lo[1], y1 = b.hi[1], z0 = b.lo[2], z1 = b.hi[2];
    auto up = [&](int k) { return std::min(y1, y0 + k); };
    std::vector<Port> want = {
        {kOil, 0, {x0, y0, z0}, kYNeg},
        {kOil, 1, {x1 > x0 ? x1 : x0, y0, x1 > x0 ? z0 : std::min(z1, z0 + 1)}, kYNeg},
        {kCoolant, 0, {x0, up(1), z0}, kXNeg},
        {kCoolant, 1, {x0, up(2) > up(1) ? up(2) : up(1), up(2) > up(1) ? z0 : std::min(z1, z0 + 1)}, kXNeg},
        {kFuel, 0, {x1, up(1), z0}, kXPos},
    };
    std::vector<Port> out;
    auto taken = [&](const Port& p) {
        for (const Used& u : used) if (u.dir == p.dir && u.pos[0] == p.pos[0] && u.pos[1] == p.pos[1] && u.pos[2] == p.pos[2]) return true;
        for (const Port& q : out) if (q.dir == p.dir && q.pos[0] == p.pos[0] && q.pos[1] == p.pos[1] && q.pos[2] == p.pos[2]) return true;
        return false;
    };
    for (Port p : want) {
        while (taken(p) && p.pos[2] < z1) ++p.pos[2];
        if (taken(p)) continue;   // no free cell on that face: that port is left out
        out.push_back(p);
    }
    return out;
}
inline std::vector<Port> ports_of(std::string_view entry) { return ports_for(bounds_of(entry), used_nodes(entry)); }

// ------------------------------------------------------------------------------------- the compact entry
inline std::string description(std::string_view stock_name) {
    return "A " + std::string(stock_name) + " with its oil, coolant and fuel ports built into the block: pipe straight into "
           "it, no manifolds needed. Oil (amber) on the bottom, coolant (blue) on the left, fuel (red) on the right. It "
           "still takes stock manifolds too. Everything else is the stock engine.";
}
inline std::string node_json(const Port& p) {
    return "{\"id\": \"" + port_id(p) + "\", \"pos\": [" + std::to_string(p.pos[0]) + ", " + std::to_string(p.pos[1]) + ", " + std::to_string(p.pos[2]) +
           "], \"direction\": " + std::to_string(p.dir) + ", \"type\": \"liquid\"}";
}
// The stock mesh path's tile base: "meshes/components/engine_block_a_0_0_0.mesh" -> "engine_block_a" (empty when it
// is not a tiled mesh in meshes/components).
inline std::string tile_base(std::string_view mesh_path) {
    constexpr std::string_view dir = "meshes/components/", tail = "_0_0_0.mesh";
    if (mesh_path.size() <= dir.size() + tail.size() || mesh_path.compare(0, dir.size(), dir) != 0 ||
        mesh_path.compare(mesh_path.size() - tail.size(), tail.size(), tail) != 0) return {};
    return std::string(mesh_path.substr(dir.size(), mesh_path.size() - dir.size() - tail.size()));
}
inline std::string stock_mesh_path(std::string_view entry) {
    std::string_view ms = find(entry, "mesh_static");
    if (ms.empty() || ms[0] != '{') return {};
    std::string_view p = find(ms, "mesh_path");
    return p.size() >= 2 && p.front() == '"' ? std::string(p.substr(1, p.size() - 2)) : std::string();
}
inline std::string mesh_base(std::string_view stock_id) { return std::string(kPrefix) + std::string(stock_id); }

// The stock definition `o` as its compact version (with our meshes when `meshes`), or empty when `o` is not one of the
// nine stock engines or its ports cannot be placed.
inline std::string compact_definition(std::string_view o, bool meshes) {
    std::vector<built::Member> ms = built::members(o);
    if (ms.empty()) return {};
    std::string_view id, name;
    for (const built::Member& m : ms) {
        if (m.key == "id") id = built::string_value(o, m);
        else if (m.key == "name") name = built::string_value(o, m);
    }
    if (!built::is_stock_engine(id) || id == "rotary_engine") return {};
    std::vector<Port> ports = ports_of(o);
    if (ports.size() < 3) return {};
    std::string stock_name = name.empty() ? std::string(id) : std::string(name);
    std::string nodes;
    for (const Port& p : ports) nodes += (nodes.empty() ? "" : ", ") + node_json(p);
    std::string_view old_nodes = find(o, "logic_nodes");
    std::string node_list;
    if (old_nodes.size() >= 2 && old_nodes.front() == '[') {
        std::string_view inner = old_nodes.substr(1, old_nodes.size() - 2);
        bool empty = built::skip_ws(inner, 0) >= inner.size();
        node_list = "[" + std::string(inner) + (empty ? "" : ", ") + nodes + "]";
    } else {
        node_list = "[" + nodes + "]";
    }
    std::string mesh;
    if (meshes && !tile_base(stock_mesh_path(o)).empty())
        mesh = "{\"mesh_path\": \"meshes/components/" + mesh_base(id) + "_0_0_0.mesh\"}";
    struct Set { const char* key; std::string value; bool done; };
    std::vector<Set> sets = {
        {"id", "\"" + std::string(kPrefix) + std::string(id) + "\"", false},
        {"name", "\"Compact " + stock_name + "\"", false},
        {"description", "\"" + description(stock_name) + "\"", false},
        {"logic_nodes", node_list, false},
        {"spawn_probability", "0.075", false},
    };
    if (!mesh.empty()) sets.push_back({"mesh_static", mesh, false});
    std::string out;
    size_t at = 0;
    for (const built::Member& m : ms)
        for (Set& s : sets) {
            if (m.key != s.key) continue;
            out.append(o.substr(at, m.value_begin - at));
            out += s.value;
            at = m.value_end;
            s.done = true;
        }
    size_t close = o.rfind('}');
    out.append(o.substr(at, close - at));
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n' || out.back() == '\r' || out.back() == '\t')) out.pop_back();
    for (Set& s : sets) if (!s.done) out += std::string(",\n      \"") + s.key + "\": " + s.value;
    out += "\n    }";
    return out;
}
// Every compact entry the stock file gives, joined with ",\n    " (for the built engines file), and their count.
// `has_mesh(stock id)` says whether that engine's compact mesh tiles were written.
template <class HasMesh>
inline std::string compact_entries(std::string_view stock_file, HasMesh has_mesh, int* count = nullptr) {
    std::string out;
    int n = 0;
    for (std::string_view o : built::definition_objects(stock_file)) {
        std::string_view id;
        for (const built::Member& m : built::members(o)) if (m.key == "id") id = built::string_value(o, m);
        std::string c = compact_definition(o, has_mesh(id));
        if (c.empty()) continue;
        if (n++) out += ",\n";
        out += "    " + c;
    }
    if (count) *count = n;
    return out;
}

// Metres of a cell's face centre on the front tile's mesh (cell (0, 0, 0) is centred on the origin, 0.08 m cells).
constexpr double kCellM = 0.08, kHalfM = 0.04;
struct Face { int axis; double sign, u, w, a; };
inline Face face_of(const Port& p) {
    const double c[3] = {p.pos[0] * kCellM, p.pos[1] * kCellM, p.pos[2] * kCellM};
    const int axis = p.dir / 2;
    const double sign = p.dir % 2 ? 1.0 : -1.0;
    // the pad's two in-face coordinates in prism order: (y, z) for x, (x, z) for y, (x, y) for z
    const double u = axis == 0 ? c[1] : c[0], w = axis == 2 ? c[1] : c[2];
    return {axis, sign, u, w, c[axis] + sign * kHalfM};
}

}  // namespace compact
