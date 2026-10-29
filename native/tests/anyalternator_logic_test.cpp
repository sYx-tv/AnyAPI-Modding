#include "../anyalternator_logic.h"
#include "../anyalternator_mesh.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace anyalternator;
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) < e * std::fmax(1.0, std::abs(b)); }

// The sizes Nate picked: 5 / 15 / 40 kW out, bigger ones less efficient, later cut-in and heavier.
static void check_sizes() {
    assert(kSizeCount == 3);
    assert(near(kSizes[0].output_watts, 5000) && near(kSizes[1].output_watts, 15000) && near(kSizes[2].output_watts, 40000));
    for (int i = 0; i < kSizeCount; ++i) {
        const Size& s = kSizes[i];
        assert(s.efficiency < kStockEfficiency && s.wattage() > s.output_watts);
        assert(s.width > 2 && s.length > 3 && s.shaft_cell() >= 0 && s.shaft_cell() < s.width);
        assert(size_of_id(s.id, std::strlen(s.id)) == i);
        if (i) {
            assert(s.efficiency < kSizes[i - 1].efficiency && s.cut_in_rps > kSizes[i - 1].cut_in_rps && s.density > kSizes[i - 1].density);
            assert(s.width >= kSizes[i - 1].width && s.length > kSizes[i - 1].length && s.pitch < kSizes[i - 1].pitch);
        }
    }
    assert(kSizes[0].tech_tier == 2 && kSizes[1].tech_tier == 2 && kSizes[2].tech_tier == 3);
    assert(size_of_id("alternator_motor", 16) == -1 && size_of_id("anyalternator_medium", 5) == -1);
}

// Output: nothing below the cut-in, full at 100 rps, never more than the game's own wattage x |rps| / 100.
static void check_factor() {
    const Size& s = kSizes[2];
    assert(output_factor(s, 0) == 0 && output_factor(s, s.cut_in_rps) == 0 && output_factor(s, -s.cut_in_rps * 0.5) == 0);
    assert(near(output_factor(s, 100), 1) && near(output_factor(s, 250), 1) && near(output_factor(s, -100), 1));
    assert(near(output_factor(s, (s.cut_in_rps + 100) / 2), 0.5));
    assert(output_factor(s, NAN) == 0);
    for (double rps = 0; rps < 200; rps += 0.5) assert(output_factor(s, rps) <= std::fmin(1.0, rps / kMaxRps) + 1e-12);
}

// One tick: the room set before the game's tick is what the game may use; the energy after carries our efficiency.
static void game_tick(const Size& s, double rps, double dt, double& energy, double capacity) {
    double factor = std::fmin(1.0, std::fabs(rps) / kMaxRps);
    if (factor <= 0) return;
    double e = std::fmin(capacity - energy, s.wattage() * factor * dt);   // alternator.tick
    energy += e * kStockEfficiency;
}
static void check_tick() {
    const Size& s = kSizes[1];
    const double dt = 1.0 / 60, capacity = 1e6;
    double energy = 1000;
    double wanted = wanted_energy(s, 100, true, dt, energy, capacity);
    assert(near(wanted, s.wattage() * dt));
    Tick t = before_tick(energy, capacity, wanted);
    assert(near(energy, capacity - wanted));
    game_tick(s, 100, dt, energy, capacity);
    Result r = after_tick(s, t, energy, capacity, dt);
    assert(near(r.used, wanted, 1e-9) && near(r.output_w, s.output_watts, 1e-9) && near(r.shaft_w, s.wattage(), 1e-9));
    assert(near(energy, 1000 + wanted * s.efficiency, 1e-9));
    assert(near(load_percent(s, r), 100, 1e-9));

    // Below the cut-in, switched off, or full: no room, so the game neither charges nor brakes.
    for (int c = 0; c < 3; ++c) {
        energy = c == 2 ? capacity : 5000;
        double before = energy;
        double w = wanted_energy(s, c == 0 ? s.cut_in_rps - 1 : 100, c != 1, dt, energy, capacity);
        assert(w == 0);
        Tick u = before_tick(energy, capacity, w);
        game_tick(s, 100, dt, energy, capacity);
        Result q = after_tick(s, u, energy, capacity, dt);
        assert(q.used == 0 && q.output_w == 0 && energy == before);
    }
    // Nearly full: limited by the real room.
    energy = capacity - 10;
    double w = wanted_energy(s, 100, true, dt, energy, capacity);
    assert(near(w, 10));
    // The game making less than we allowed (the shaft slowed this tick): we count what it used.
    energy = 0;
    Tick u = before_tick(energy, capacity, wanted_energy(s, 100, true, dt, energy, capacity));
    game_tick(s, 50, dt, energy, capacity);
    Result q = after_tick(s, u, energy, capacity, dt);
    assert(near(q.used, s.wattage() * 0.5 * dt, 1e-9) && near(energy, q.used * s.efficiency, 1e-9));
}

// Definitions: ids, numbers and surfaces that cover every face except the torque cell.
static void check_definitions() {
    std::string meshes[kSizeCount];
    for (int i = 0; i < kSizeCount; ++i) meshes[i] = mesh_path(kSizes[i]);
    std::string json = definitions_json(meshes);
    for (const Size& s : kSizes) {
        assert(json.find(std::string("\"id\": \"") + s.id + "\"") != std::string::npos);
        assert(json.find(mesh_path(s)) != std::string::npos);
    }
    assert(json.find("\"class\": \"alternator\"") != std::string::npos && json.find("\"enabled\"") != std::string::npos);
    assert(json.find("\"alternator_wattage\": 7142.9") != std::string::npos);
    assert(json.find("\"bounds_min\": [0, 0, -6], \"bounds_max\": [4, 4, 1]") != std::string::npos);   // Heavy: 5 x 5 x 8
    assert(json.find("{\"id\": \"torque\", \"pos\": [2, 2, 1]") != std::string::npos);
    // The +z end of the 4 x 4: shaft at (1, 1); the four front ports cover the other 15 cells.
    std::string d = definition(kSizes[1], meshes[1]);
    assert(d.find(port(0, 0, 1, 5, 4, 1)) != std::string::npos && d.find(port(0, 1, 1, 5, 1, 1)) != std::string::npos &&
           d.find(port(2, 1, 1, 5, 2, 1)) != std::string::npos && d.find(port(0, 2, 1, 5, 4, 2)) != std::string::npos);
    assert(description(kSizes[2]).find("40 kW") != std::string::npos && description(kSizes[0]).find("5.0 kW") != std::string::npos);
}

// Mesh: a small stand-in for the stock alternator (one quad on its +z plate and a hub quad on the axis) grows to size.
static std::vector<uint8_t> stand_in() {
    std::vector<MeshVertex> v;
    auto add = [&](float x, float y, float z, uint8_t r, uint8_t g, uint8_t b) {
        MeshVertex m{}; m.pos[0] = x; m.pos[1] = y; m.pos[2] = z; m.rgba[0] = r; m.rgba[1] = g; m.rgba[2] = b; m.rgba[3] = 255; m.normal[2] = 1;
        v.push_back(m);
    };
    add(-0.04f, -0.04f, -0.12f, 153, 0, 0); add(0.12f, 0.12f, 0.12f, 153, 0, 0); add(0.12f, -0.04f, 0.12f, 153, 0, 0);
    add(-0.01f, -0.01f, 0.13f, 198, 198, 198); add(0.01f, -0.01f, 0.13f, 198, 198, 198); add(0.01f, 0.01f, 0.13f, 198, 198, 198);
    std::vector<uint32_t> idx = {0, 1, 2, 3, 4, 5};
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n) { auto* q = static_cast<const uint8_t*>(p); b.insert(b.end(), q, q + n); };
    uint32_t u;
    put("mesh", 4); u = 5; put(&u, 4); u = 1; put(&u, 4); u = 4; put(&u, 4); put("test", 4);
    std::vector<uint8_t> zero(88 + 48, 0); put(zero.data(), zero.size());
    u = uint32_t(v.size() * sizeof(MeshVertex)); put(&u, 4); put(v.data(), v.size() * sizeof(MeshVertex));
    u = uint32_t(idx.size() * 4); put(&u, 4); put(idx.data(), idx.size() * 4);
    put("trailer!", 8);
    return b;
}
static void check_mesh() {
    std::vector<uint8_t> stock = stand_in();
    MeshLayout m;
    assert(parse_mesh(stock, m) && m.vertices == 6);
    for (const Size& s : kSizes) {
        std::vector<uint8_t> out = build_mesh(stock, s);
        MeshLayout o;
        assert(!out.empty() && parse_mesh(out, o) && o.vertices > m.vertices);
        double bounds[6];
        std::memcpy(bounds, out.data() + o.bounds, sizeof bounds);
        assert(near(bounds[0], -0.04, 1e-5) && near(bounds[3], kCell * s.width - 0.04, 1e-5));
        assert(near(bounds[2], 0.12 - kCell * s.length, 1e-5));
        std::vector<MeshVertex> v(o.vertices);
        std::memcpy(v.data(), out.data() + o.vertex_bytes + 4, v.size() * sizeof(MeshVertex));
        float c = float(kCell * s.shaft_cell());
        assert(std::fabs((v[3].pos[0] + v[4].pos[0]) / 2 - c) < 1e-5f && v[3].pos[2] > 0.13f);   // the hub moved onto the torque cell
    }
    std::vector<uint8_t> wrong = stock;
    wrong[4] = 4;
    assert(build_mesh(wrong, kSizes[0]).empty());
}

int main() {
    check_sizes();
    check_factor();
    check_tick();
    check_definitions();
    check_mesh();
    std::puts("anyalternator_logic: ok");
}
