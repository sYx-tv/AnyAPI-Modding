#include "../anycvt_logic.h"
#include "../anycvt_mesh.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace anycvt;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e * std::fmax(1.0, std::abs(b)); }

// Small, medium and large, built on the stock gearbox of that size; tiers as stock, rarity as AnyEngineParts.
static void check_sizes() {
    assert(kSizeCount == 3);
    assert(!std::strcmp(kSizes[0].stock_id, "gearbox") && !std::strcmp(kSizes[1].stock_id, "gearbox_b") && !std::strcmp(kSizes[2].stock_id, "gearbox_c"));
    assert(kSizes[0].tech_tier == 2 && kSizes[1].tech_tier == 3 && kSizes[2].tech_tier == 4);
    assert(near(kSizes[0].spawn_probability, 0.15) && near(kSizes[1].spawn_probability, 0.15) && near(kSizes[2].spawn_probability, 0.075));
    assert(!kSizes[0].add_shifter && !kSizes[0].mesh && kSizes[1].add_shifter && kSizes[2].add_shifter);
    for (int i = 0; i < kSizeCount; ++i) assert(size_of_id(kSizes[i].id, std::strlen(kSizes[i].id)) == i);
    assert(size_of_id("gearbox", 7) == -1 && size_of_id("anycvt_small", 6) == -1 && size_of_id(nullptr, 0) == -1);
    assert(mesh_path(kSizes[0]) == "meshes/components/gear_box_a.mesh" && mesh_path(kSizes[2]) == "meshes/components/anycvt_large.mesh");
    assert(stock_mesh_path(kSizes[1]) == "meshes/components/gear_box_b.mesh");
}

// Five rows on slots 0-4, a selector in 5, a marker in 7; a new gearbox (stock 24, 18, 14, ...) gets the defaults once.
static void check_settings() {
    int32_t slots[kSlots] = {24, 18, 14, 10, 6, 4, 2, 1};
    assert(ensure_defaults(slots));
    assert(slots[0] == 24 && slots[1] == 1 && slots[2] == 6800 && slots[3] == 5 && slots[4] == 1500 && slots[kSelectorSlot] == 0 && slots[kMarkerSlot] == kMarker);
    slots[2] = 5000;
    assert(!ensure_defaults(slots) && slots[2] == 5000);
    Settings s = read_settings(slots);
    assert(s.low == 24 && s.high == 1 && s.target_rpm == 5000 && s.response == 5 && s.engage_rpm == 1500);
    slots[0] = 2; slots[1] = 30; slots[3] = 99; slots[4] = 0;   // either way round; out of range clamps
    s = read_settings(slots);
    assert(s.low == 30 && s.high == 2 && s.response == 10 && s.engage_rpm == 400);
    for (int i = 0; i < kSettingCount; ++i) {
        const Setting& r = kSettings[i];
        assert(r.slot == i && r.min < r.max && r.fallback >= r.min && r.fallback <= r.max && r.text && *r.text);
        assert(setting_of_row(r.label, r.min, r.max) == i);
    }
    assert(setting_of_row(341, 1, 100) == -1 && setting_of_row(326, 1000, 8001) == -1);
}

// R, N, D stepped by the link like stock gears: past 0.75 steps, back inside 0.25 re-arms, ends hold.
static void check_selector() {
    Shifter sh;
    int sel = 0;
    sel = step_selector(sh, sel, 0.9); assert(sel == 1);
    sel = step_selector(sh, sel, 1.0); assert(sel == 1);    // held: no repeat
    sel = step_selector(sh, sel, 0.5); assert(sel == 1);    // not re-armed yet
    sel = step_selector(sh, sel, -0.9); assert(sel == 1);   // still not re-armed
    sel = step_selector(sh, sel, 0.1); assert(sel == 1 && sh.armed);
    sel = step_selector(sh, sel, 0.9); assert(sel == 1);    // D is the top
    sel = step_selector(sh, sel, 0); sel = step_selector(sh, sel, -0.8); assert(sel == 0);
    sel = step_selector(sh, sel, 0); sel = step_selector(sh, sel, -0.8); assert(sel == -1);
    sel = step_selector(sh, sel, 0); sel = step_selector(sh, sel, -0.8); assert(sel == -1);   // R is the bottom
    sel = step_selector(sh, sel, NAN); assert(sel == -1);
    assert(clamp_selector(7) == 1 && clamp_selector(-3) == -1 && clamp_selector(0) == 0);
}

// The engine speed D holds and the clutch.
static void check_goal_and_clutch() {
    Settings s;
    assert(near(goal_rpm(s, 0), 1500) && near(goal_rpm(s, 1), 6800) && near(goal_rpm(s, 0.5), 4150) && near(goal_rpm(s, 2), 6800) && near(goal_rpm(s, NAN), 1500));
    s.target_rpm = 1000;   // a target under the engage RPM holds the engage RPM
    assert(near(goal_rpm(s, 1), 1500));
    s = Settings{};
    assert(clutch_at(s, 0) == 0 && clutch_at(s, 1500) == 0 && clutch_at(s, NAN) == 0);
    assert(near(clutch_at(s, 1500 + 300), 1) && near(clutch_at(s, 1650), 0.5) && clutch_at(s, 9000) == 1);
    for (double rpm = 1400; rpm < 2000; rpm += 10) assert(clutch_at(s, rpm + 10) >= clutch_at(s, rpm));
    s.engage_rpm = 800;   // the band is never narrower than 250 rpm
    assert(near(clutch_at(s, 1050), 1) && clutch_at(s, 1049) < 1);
}

// A launch: starts at the low ratio, the clutch bites, the ratio slides toward high as the output speeds up.
static void check_drive() {
    Settings s;
    State st;
    Inputs in;
    in.selector = 1; in.engine = true; in.throttle = 1; in.engine_rpm = 1200; in.output_rps = 0;
    Outputs o = step(s, st, in);
    assert(near(o.ratio, 24) && near(o.node_ratio, 24) && o.clutch == 0 && near(o.goal_rpm, 6800));
    in.engine_rpm = 6800;
    o = step(s, st, in);
    assert(near(o.ratio, 24) && o.clutch == 1);
    // Output at 10 rps: holding 6,800 rpm wants 113.3 / 10 = 11.33, slid toward over time.
    in.output_rps = 10;
    double last = o.ratio;
    for (int i = 0; i < 600; ++i) { o = step(s, st, in); assert(o.ratio <= last + 1e-12 && o.ratio >= 6800.0 / 60 / 10 - 1e-9); last = o.ratio; }
    assert(near(o.ratio, 6800.0 / 60 / 10, 1e-6));
    // Faster than the high ratio can hold: stops at high.
    in.output_rps = 500;
    for (int i = 0; i < 600; ++i) o = step(s, st, in);
    assert(near(o.ratio, 1));
    // Response: 10 slides further in one tick than 1.
    Settings fast = s, slow = s;
    fast.response = 10; slow.response = 1;
    State a, b;
    in.output_rps = 10;
    Outputs fa = step(fast, a, in), fb = step(fast, a, in);
    Outputs sa = step(slow, b, in), sb = step(slow, b, in);
    assert(near(fa.ratio, 24 - (24 - 6800.0 / 600) * (1 - std::exp(-6.0 / 60))) && fb.ratio < sb.ratio && sa.ratio > fa.ratio);
    // Throttle off: the goal drops to the engage RPM, so the ratio heads toward high (engine braking stays on).
    in.throttle = 0; in.output_rps = 10; in.engine_rpm = 3000;
    State c{6};
    o = step(s, c, in);
    assert(near(o.goal_rpm, 1500) && o.ratio < 6 && o.clutch == 1);
}

// N, R, no engine, the Output/Input and Reverse properties, and waiting for the output to stop.
static void check_neutral_reverse() {
    Settings s;
    Inputs in;
    in.engine = true; in.throttle = 1; in.engine_rpm = 5000;
    State st;
    in.selector = 0; in.output_rps = 10;
    Outputs o = step(s, st, in);
    assert(o.clutch == 0 && o.ratio < 24);   // N: open, but the ratio still follows the output
    in.selector = -1; in.output_rps = 0;
    st = State{};
    o = step(s, st, in);
    assert(near(o.ratio, 24) && near(o.node_ratio, -24) && o.clutch == 1);
    in.output_rps = -15;   // already backing up fast: R holds the low ratio
    for (int i = 0; i < 120; ++i) o = step(s, st, in);
    assert(near(o.ratio, 24) && o.clutch == 1);
    in.output_rps = 15;    // still rolling forward: R waits
    o = step(s, st, in);
    assert(o.clutch == 0 && near(o.node_ratio, -24));
    in.selector = 1; in.output_rps = -15;   // D while rolling back: waits too
    o = step(s, st, in);
    assert(o.clutch == 0);
    in.output_rps = -1.5;  // slow enough: bites
    o = step(s, st, in);
    assert(o.clutch == 1);
    in.engine = false;     // no engine on its face: open
    o = step(s, st, in);
    assert(o.clutch == 0 && near(o.goal_rpm, 1500));
    // Reverse ticked (direction -1): D drives the node negative and counts a negative output as forward.
    in.engine = true; in.direction = -1; in.output_rps = -15;
    st = State{};
    o = step(s, st, in);
    assert(o.node_ratio < 0 && o.clutch == 1);
    in.output_rps = 15;
    o = step(s, st, in);
    assert(o.clutch == 0);
    in.direction = 0.5;    // Output / Input scales the node ratio
    st = State{};
    in.output_rps = 0;
    o = step(s, st, in);
    assert(near(o.node_ratio, 12));
    in.output_rps = 10;    // and the belt holds the engine at the goal through it
    for (int i = 0; i < 1200; ++i) o = step(s, st, in);
    assert(near(o.node_ratio, 6800.0 / 60 / 10, 1e-6) && near(o.ratio, 2 * 6800.0 / 60 / 10, 1e-6));
    in.direction = NAN; in.dt = NAN;
    o = step(s, st, in);
    assert(std::isfinite(o.node_ratio) && std::isfinite(o.ratio));
}

static void check_split() {
    auto rects = split_around({-1, 2, 3, 3}, 0, 4);   // the large's 3x3 top port, shifter at the back middle
    int cells = 0;
    for (auto& r : rects) {
        cells += r.sx * r.sz;
        assert(!(0 >= r.x && 0 < r.x + r.sx && 4 >= r.z && 4 < r.z + r.sz));
    }
    assert(cells == 8);
    rects = split_around({0, 2, 1, 2}, 0, 3);         // the medium's 1x2 top port
    assert(rects.size() == 1 && rects[0].x == 0 && rects[0].z == 2 && rects[0].sx == 1 && rects[0].sz == 1);
    rects = split_around({0, 2, 1, 2}, 5, 3);         // not on it: unchanged
    assert(rects.size() == 1 && rects[0].sz == 2);
    rects = split_around({0, 0, 3, 3}, 1, 1);         // a middle cell: four pieces, eight cells
    cells = 0;
    for (auto& r : rects) cells += r.sx * r.sz;
    assert(rects.size() == 4 && cells == 8);
}

// A made-up definitions file shaped like the game's (no game data in the repo).
static const char* kFakeStock = R"({
  "definitions": [
    {"id": "engine", "class": "engine", "surfaces": [], "zones": []},
    {"id": "gearbox", "name": "G", "class": "gearbox", "description": "d", "tech_tier": 9, "spawn_probability": 0.9,
     "mesh_static": {"mesh_path": "meshes/components/gear_box_a.mesh", "other": 1},
     "mode_z": "stretch", "interval": [0, 0, 1], "center_stretch": [0, 0, -1],
     "zones": [{"min": [-1, -1, -1], "max": [1, 1, 1]}],
     "surfaces": [{"pos": [0, 1, 1], "dir": 3, "gender": 2, "type": "mechanical"}, {"id": "torque", "pos": [0, 0, -1], "dir": 4, "type": "torque"}],
     "logic_nodes": [{"id": "gear_shift", "pos": [0, 1, 1], "direction": 3, "type": "mechanical_in"}],
     "data_descriptors": [{"type": "type_bool_output", "name": "is_reverse"}], "tags_generated": "mechanical,torque"},
    {"id": "gearbox_b", "class": "gearbox_b", "mesh_static": {"mesh_path": "meshes/components/gear_box_b.mesh"},
     "zones": [{"min": [-2, -2, -2], "max": [2, 2, 3]}],
     "surfaces": [{"pos": [0, 2, 2], "size": [1, 2], "dir": 3, "type": "port"}, {"pos": [0, -2, 2], "size": [1, 2], "dir": 2, "type": "port"},
                  {"id": "torque_engine", "pos": [0, 0, 3], "dir": 5, "type": "torque_engine_b"}],
     "logic_nodes": [], "tags_generated": "torque_engine_b,torque"},
    {"id": "gearbox_c", "class": "gearbox_c", "mesh_static": {"mesh_path": "meshes/components/gear_box_c.mesh"},
     "zones": [{"min": [-3, -3, -4], "max": [3, 3, 4]}],
     "surfaces": [{"pos": [-1, 3, 2], "size": [3, 3], "dir": 3, "type": "port"}, {"id": "torque_engine", "pos": [0, 0, 4], "dir": 5, "type": "torque_engine_c"}],
     "tags_generated": "torque_engine_c"}
  ]
})";

static void check_definitions() {
    std::string meshes[kSizeCount] = {mesh_path(kSizes[0]), mesh_path(kSizes[1]), stock_mesh_path(kSizes[2])};
    std::string error;
    std::string json = definitions_json(kFakeStock, meshes, &error);
    assert(!json.empty() && error.empty());
    assetjson::Value root = assetjson::Parser(json).parse();
    const assetjson::Value& defs = root.get("definitions");
    assert(defs.kind == 'a' && defs.array.size() == 3);
    for (int i = 0; i < kSizeCount; ++i) {
        const assetjson::Value& d = defs.array[i];
        assert(d.get("id").text == kSizes[i].id && d.get("name").text == kSizes[i].name && d.get("class").text == "gearbox");
        assert(as_int(d.get("tech_tier"), 0) == kSizes[i].tech_tier && near(d.get("spawn_probability").number, kSizes[i].spawn_probability));
        assert(d.get("mesh_static").get("mesh_path").text == meshes[i]);
        assert(d.get("mode_z").kind == 'n' && d.get("interval").kind == 'n' && d.get("center_stretch").kind == 'n');
        assert(d.get("description").text.find(kSizes[i].engine) != std::string::npos);
        const assetjson::Value& data = d.get("data_descriptors");
        assert(data.array.size() == 6 && data.array[0].get("name").text == "ratio" && data.array[4].get("name").text == "clutch_factor");
        assert(data.array[5].get("name").text == "current_gear" && data.array[5].get("type").text == "type_s32_output" && data.array[5].get("documentation").text == "-1 in R, 0 in N, 1 in D.");
        assert(data.array[3].get("name").text == "selector" && data.array[3].get("documentation").text == "-1 in R, 0 in N, 1 in D.");
        assert(d.get("description").text.find("-1 in R, 0 in N, 1 in D") != std::string::npos);
        assert(d.get("zones").array.size() == 1);
        const assetjson::Value& nodes = d.get("logic_nodes");
        assert(nodes.array.size() == 1 && nodes.array[0].get("id").text == "gear_shift");
        int p[3];
        assert(vec3(nodes.array[0].get("pos"), p) && p[0] == kSizes[i].shifter.x && p[1] == kSizes[i].shifter.y && p[2] == kSizes[i].shifter.z);
        const assetjson::Value* mech = find_of_type(d.get("surfaces"), "mechanical");
        assert(mech && vec3(mech->get("pos"), p) && p[0] == kSizes[i].shifter.x && p[1] == kSizes[i].shifter.y && p[2] == kSizes[i].shifter.z);
        assert(as_int(mech->get("dir"), 0) == 3 && as_int(mech->get("gender"), 0) == 2);
        assert(d.get("tags_generated").text.find("mechanical") != std::string::npos);
    }
    assert(defs.array[0].get("mesh_static").get("other").number == 1);   // other stock fields kept
    // The medium: its 1x2 top port loses the shifter cell, the bottom port and the engine face are untouched.
    const assetjson::Value& medium = defs.array[1].get("surfaces");
    assert(medium.array.size() == 4);
    int ports_top = 0;
    for (auto& f : medium.array) {
        int p[3];
        vec3(f.get("pos"), p);
        if (f.get("type").text == "port" && as_int(f.get("dir"), 0) == 3) { ++ports_top; assert(p[2] == 2 && f.get("size").array[1].number == 1); }
        if (f.get("type").text == "torque_engine_b") assert(p[2] == 3);
    }
    assert(ports_top == 1);
    assert(defs.array[2].get("surfaces").array.size() == 1 + 3 + 1);   // engine face, the 3x3 port in three pieces, shifter
    // Missing pieces fail with a reason.
    assert(definitions_json("{\"definitions\": []}", meshes, &error).empty() && !error.empty());
    assert(definitions_json("not json", meshes, &error).empty());
    assert(definitions_json("{}", meshes, &error).empty());
}

// A made-up mesh: a box with a raised pad of triangles on top for the "small", plain boxes for the others.
static void box(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, float x0, float y0, float z0, float x1, float y1, float z1) {
    float c[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    uint32_t base = uint32_t(v.size());
    for (auto& p : c) { MeshVertex m{}; m.pos[0] = p[0]; m.pos[1] = p[1]; m.pos[2] = p[2]; m.normal[1] = 1; v.push_back(m); }
    uint32_t faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7}, {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};
    for (auto& f : faces) for (int k = 0; k < 3; ++k) idx.push_back(base + f[k]);
}
static std::vector<uint8_t> mesh_file(const char* name, const std::vector<MeshVertex>& v, const std::vector<uint32_t>& idx) {
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) { auto* q = static_cast<const uint8_t*>(p); out.insert(out.end(), q, q + n); };
    uint32_t u = 5, one = 1, n = uint32_t(std::strlen(name));
    put("mesh", 4); put(&u, 4); put(&one, 4); put(&n, 4); put(name, n);
    uint8_t header[88]{}; header[0] = 7; put(header, 88);
    double bounds[6]{}; put(bounds, 48);
    u = uint32_t(v.size() * sizeof(MeshVertex)); put(&u, 4); put(v.data(), v.size() * sizeof(MeshVertex));
    u = uint32_t(idx.size() * 4); put(&u, 4); put(idx.data(), idx.size() * 4);
    uint8_t trailer[8] = {1, 2, 3, 4, 5, 6, 7, 8}; put(trailer, 8);
    return out;
}
static void check_mesh() {
    std::vector<MeshVertex> v; std::vector<uint32_t> idx;
    box(v, idx, -0.12f, -0.12f, -0.12f, 0.12f, 0.12f, 0.2f);   // the case, top at 0.12 (its top face spans the pad: not taken)
    for (int i = 0; i < 12; ++i) {                              // the pad: 24 flat triangles on the top over the shifter cell
        float z = 0.05f + 0.005f * i;
        for (int k = 0; k < 2; ++k) {
            uint32_t base = uint32_t(v.size());
            float xs[3][2] = {{-0.03f, z}, {0.03f, z}, {0.0f, z + 0.005f}};
            for (auto& p : xs) { MeshVertex m{}; m.pos[0] = p[0] * (k ? -1 : 1); m.pos[1] = 0.12f; m.pos[2] = p[1]; m.normal[1] = 1; m.rgba[0] = 200; v.push_back(m); }
            for (int j = 0; j < 3; ++j) idx.push_back(base + j);
        }
    }
    std::vector<uint8_t> small_mesh = mesh_file("gear_box_a", v, idx);
    MeshLayout m;
    assert(parse_mesh(small_mesh, m) && m.vertices == v.size() && m.indices == idx.size());
    std::vector<MeshVertex> pad = shifter_connector(small_mesh);
    assert(pad.size() == 24 * 3);
    for (auto& p : pad) assert(p.rgba[0] == 200);

    std::vector<MeshVertex> v2; std::vector<uint32_t> i2;
    box(v2, i2, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.28f);
    std::vector<uint8_t> medium = mesh_file("gear_box_b", v2, i2);
    std::vector<uint8_t> built = build_mesh(medium, pad, kSizes[1]);
    MeshLayout b;
    assert(!built.empty() && parse_mesh(built, b) && b.vertices == v2.size() + pad.size() && b.indices == i2.size() + pad.size());
    std::vector<MeshVertex> out = vertices_of(built, b);
    for (size_t k = v2.size(); k < out.size(); ++k) {
        assert(near(out[k].pos[1], 0.2005, 1e-5));
        assert(out[k].pos[2] >= 0.08f * 3 - 0.031f && out[k].pos[2] <= 0.08f * 3 + 0.031f);
    }
    double bounds[6];
    std::memcpy(bounds, built.data() + b.bounds, 48);
    assert(near(bounds[4], 0.2005, 1e-5) && near(bounds[0], -0.2, 1e-6));
    assert(std::memcmp(built.data() + b.trailer, small_mesh.data() + small_mesh.size() - 8, 8) == 0);
    assert(std::string(reinterpret_cast<const char*>(built.data()) + 16, 13) == "anycvt_medium");
    assert(build_mesh(medium, pad, kSizes[2]).empty());   // not the large's top: refused
    assert(build_mesh(medium, pad, kSizes[0]).empty());   // the small has no mesh of its own
    assert(build_mesh(medium, {}, kSizes[1]).empty());
    std::vector<uint8_t> broken = medium; broken.pop_back();
    assert(build_mesh(broken, pad, kSizes[1]).empty() && shifter_connector(broken).empty());
    assert(shifter_connector(medium).empty());            // top not at 0.12
}

int main() {
    check_sizes();
    check_settings();
    check_selector();
    check_goal_and_clutch();
    check_drive();
    check_neutral_reverse();
    check_split();
    check_definitions();
    check_mesh();
    std::puts("anycvt_logic_test: ok");
    return 0;
}
