#include "../compact_engine_logic.h"
#include "../compact_engine_mesh.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace compact;

// A made-up engine entry shaped like the game's (not game data): 3 x 6 cells, two long, one gas node.
static const char* kStock = R"({"definitions": [
    {"id": "engine", "name": "Inline Engine", "class": "engine",
     "zones": [{"bounds_min": [-1, -1, 0], "bounds_max": [1, 4, 1]}],
     "surfaces": [{"pos": [0, -1, 0], "size": [1, 2], "dir": 2, "gender": 2, "type": "port"}],
     "logic_nodes": [{"id": "exhaust", "pos": [-1, 0, 0], "direction": 0, "type": "gas"}],
     "mesh_static": {"mesh_path": "meshes/components/engine_block_a_0_0_0.mesh"},
     "spawn_probability": 0.1},
    {"id": "radiator", "name": "Radiator", "class": "radiator", "zones": [{"bounds_min": [0, 0, 0], "bounds_max": [1, 1, 1]}]},
    {"id": "rotary_engine", "name": "Rotary", "class": "engine", "zones": [{"bounds_min": [-1, -1, 0], "bounds_max": [1, 4, 1]}]}
]})";

int main() {
    auto objs = built::definition_objects(kStock);
    assert(objs.size() == 3);
    Box b = bounds_of(objs[0]);
    assert(b.ok() && b.lo[0] == -1 && b.hi[0] == 1 && b.lo[1] == -1 && b.hi[1] == 4 && b.lo[2] == 0 && b.hi[2] == 1);
    std::vector<Port> ports = ports_of(objs[0]);
    assert(ports.size() == 5);
    // oil on the bottom at both columns, coolant on the left (the exhaust's cell taken: moved back a cell), fuel right
    assert(ports[0].kind == kOil && ports[0].dir == kYNeg && ports[0].pos[0] == -1 && ports[0].pos[1] == -1 && ports[0].pos[2] == 0);
    assert(ports[1].kind == kOil && ports[1].pos[0] == 1 && ports[1].dir == kYNeg);
    assert(ports[2].kind == kCoolant && ports[2].dir == kXNeg && ports[2].pos[1] == 0 && ports[2].pos[2] == 1);   // (-1, 0, 0) x- is the exhaust's
    assert(ports[3].kind == kCoolant && ports[3].pos[1] == 1 && ports[3].pos[2] == 0);
    assert(ports[4].kind == kFuel && ports[4].dir == kXPos && ports[4].pos[0] == 1 && ports[4].pos[1] == 0);
    assert(port_id(ports[3]) == "compact_coolant_1");
    int idx = -1;
    assert(kind_of_node("compact_fuel_0", &idx) == kFuel && idx == 0 && kind_of_node("compact_oil_1", &idx) == kOil && idx == 1);
    assert(kind_of_node("exhaust") == -1 && kind_of_node("compact_water_0") == -1);
    assert(is_compact_id("compact_v_engine") && !is_compact_id("compact_rotary_engine") && !is_compact_id("v_engine") && !is_compact_id("compact_radiator"));

    std::string c = compact_definition(objs[0], true);
    assert(!c.empty());
    assert(c.find("\"id\": \"compact_engine\"") != std::string::npos && c.find("\"name\": \"Compact Inline Engine\"") != std::string::npos);
    assert(c.find("{\"id\": \"exhaust\"") != std::string::npos);   // the stock node stays first
    assert(c.find("{\"id\": \"exhaust\", \"pos\": [-1, 0, 0], \"direction\": 0, \"type\": \"gas\"}, {\"id\": \"compact_oil_0\"") != std::string::npos);
    assert(c.find("\"mesh_path\": \"meshes/components/compact_engine_0_0_0.mesh\"") != std::string::npos);
    assert(c.find("\"spawn_probability\": 0.075") != std::string::npos);
    assert(compact_definition(objs[0], false).find("engine_block_a_0_0_0.mesh") != std::string::npos);
    assert(compact_definition(objs[1], true).empty() && compact_definition(objs[2], true).empty());
    // the rewritten entry is one well-formed object, and parses back with the same ports
    assert(built::members(c).size() == built::members(objs[0]).size() + 1);   // + description
    std::vector<Port> again = ports_for(bounds_of(c), {});
    assert(again.size() == 5);
    int n = 0;
    std::string all = compact_entries(kStock, [](std::string_view) { return true; }, &n);
    assert(n == 1 && !all.empty());
    assert(tile_base("meshes/components/engine_block_a_0_0_0.mesh") == "engine_block_a" && tile_base("meshes/x.mesh").empty());

    // a pad lands on its face: the bottom oil pad at y = -1 cell's bottom (-0.12 m)
    Face f = face_of(ports[0]);
    assert(f.axis == 1 && f.sign < 0 && std::abs(f.a + 0.12) < 1e-9 && std::abs(f.u + 0.08) < 1e-9 && std::abs(f.w) < 1e-9);
    Face g = face_of(ports[4]);
    assert(g.axis == 0 && g.sign > 0 && std::abs(g.a - 0.12) < 1e-9 && std::abs(g.u) < 1e-9 && std::abs(g.w) < 1e-9);
    for (int style = 0; style < 3; ++style) {
        pdmesh::Mesh m;
        compactmesh::pad(m, style, f.axis, f.sign, f.u, f.w, f.a, kOil);
        assert(!m.v.empty() && m.i.size() % 3 == 0);
        for (auto& v : m.v) assert(v.pos[1] <= -0.12 + 1e-4 && v.pos[1] > -0.12 - 0.02);   // proud of the bottom face, outward
    }
    {   // a mesh file decodes back to what was encoded
        pdmesh::Mesh m;
        compactmesh::pad(m, 1, 0, 1.0, 0.0, 0.0, 0.12, kFuel);
        std::vector<uint8_t> b = pdmesh::encode(m, "t");
        pdmesh::Mesh d;
        assert(compactmesh::decode(b, d) && d.v.size() == m.v.size() && d.i == m.i && !compactmesh::decode(std::vector<uint8_t>(b.begin(), b.end() - 1), d));
    }
    bool is_built = true;
    assert(built::stock_id("compact_v_engine", &is_built) == "v_engine" && !is_built && built::stock_id("built_engine", &is_built) == "engine" && is_built);
    std::puts("compact_engine_logic ok");
}
