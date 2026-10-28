#include "../anybattery_logic.h"
#include "../anybattery_mesh.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace anybattery;
static bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) < e * std::max(1.0, std::abs(b)); }

// Capacity: 0.5 kWh per cell matches the stock batteries (2 x 2 x 2 cells = 4 kWh); stretching adds whole cells.
static void check_capacity() {
    Settings s;
    assert(cells(0, 0, 0) == 8 && near(capacity_joules(cells(0, 0, 0), s), 4 * kJoulesPerKwh));
    assert(cells(1, 0, 0) == 12 && near(capacity_joules(cells(1, 0, 0), s), 6 * kJoulesPerKwh));
    assert(cells(2, 2, 2) == 64 && near(capacity_joules(cells(2, 2, 2), s), 32 * kJoulesPerKwh));
    assert(cells(-3, 0, 0) == cells(3, 0, 0));                        // extend sign ignored
    assert(cells(1000, 0, 0) == int64_t(kBaseCells + kMaxExtend) * 4); // past ext_max: clamped
    s.kwh_per_cell = 1.0;
    assert(near(capacity_joules(8, s), 8 * kJoulesPerKwh));
    Settings bad;
    bad.kwh_per_cell = -1; bad.drain_percent_per_hour = NAN; bad.start_percent = 400; bad.damage = DamageMode(7);
    bad = clamp(bad);
    assert(near(bad.kwh_per_cell, 0.1) && bad.drain_percent_per_hour == 0 && bad.start_percent == 100 && bad.damage == kDrainAll);
}

// Charge between ticks.
static void check_ticks() {
    Settings s;
    const double cap = capacity_joules(8, s);
    // A new pack: the game created it with capacity 0. Placed dead.
    {
        Pack p; Content c{0, 0};
        double before = before_tick(p, c, cap, s, nullptr);
        assert(p.seen && c.capacity == cap && c.energy == 0 && before == 0);
        after_tick(p, c, false, before, s, 1.0 / 60);
        assert(c.energy == 0);
        // The network charges it between ticks: kept.
        c.energy = 1000;
        before = before_tick(p, c, cap, s, nullptr);
        assert(c.energy == 1000 && p.energy == 1000);
        // The game zeroes the capacity (build_accessory with no item), clamping the energy: ours is put back.
        c.capacity = 0; c.energy = 0;
        before_tick(p, c, cap, s, nullptr);
        assert(c.capacity == cap && c.energy == 1000);
        // Overcharged by the network: clamped to the capacity.
        c.energy = cap * 2;
        before_tick(p, c, cap, s, nullptr);
        assert(c.energy == cap);
    }
    // Placed with a starting charge setting.
    {
        Settings half = s; half.start_percent = 50;
        Pack p; Content c{0, 0};
        before_tick(p, c, cap, half, nullptr);
        assert(near(c.energy, cap / 2) && near(charge_percent(c), 50));
    }
    // Loaded: the saved charge wins over what the game has (0, from create).
    {
        Pack p; Content c{0, 0};
        double saved = 12345;
        before_tick(p, c, cap, s, &saved);
        assert(c.energy == 12345);
    }
    // Split off another vehicle: new ids, nothing saved, but the content is already ours: it keeps its charge.
    {
        Pack p; Content c{777, cap};
        before_tick(p, c, cap, s, nullptr);
        assert(c.energy == 777);
    }
    // Capacity setting lowered: the charge is clamped to the new capacity.
    {
        Pack p; Content c{0, 0};
        double saved = cap;
        before_tick(p, c, cap, s, &saved);
        before_tick(p, c, cap / 4, s, nullptr);
        assert(c.capacity == cap / 4 && c.energy == cap / 4);
    }
    // Crash damage: the game's tick empties it; the setting decides what is left.
    for (int mode = 0; mode < kDamageModes; ++mode) {
        Settings d = s; d.damage = DamageMode(mode);
        Pack p; Content c{0, 0};
        double saved = 1000;
        double before = before_tick(p, c, cap, d, &saved);
        c.energy = 0;                                          // what the stock tick does on pending damage
        after_tick(p, c, true, before, d, 1.0 / 60);
        assert(c.energy == (mode == kDrainAll ? 0.0 : mode == kDrainHalf ? 500.0 : 1000.0) && p.energy == c.energy);
    }
    // Self-discharge: 10% per hour loses about 10% over an hour of 60 Hz ticks; dt outside 0-1 s is skipped.
    {
        Settings d = s; d.drain_percent_per_hour = 10;
        Pack p; Content c{0, 0};
        double saved = cap;
        before_tick(p, c, cap, d, &saved);
        for (int t = 0; t < 3600 * 60; ++t) after_tick(p, c, false, c.energy, d, 1.0 / 60);
        assert(near(charge_percent(c), 100 * std::exp(-0.1), 1e-3));
        double kept = c.energy;
        after_tick(p, c, false, c.energy, d, 5.0);
        assert(c.energy == kept);
    }
}

static void check_store() {
    ChargeMap m;
    m[Key{12, 34}] = 1440000.0;
    m[Key{-1, 7}] = 0.0;
    m[Key{5, 6}] = NAN;                                        // skipped
    std::string text = encode_charge(m);
    ChargeMap back = decode_charge(text);
    assert(back.size() == 2 && back[(Key{12, 34})] == 1440000.0 && back[(Key{-1, 7})] == 0.0);
    // Junk and odd keys are skipped, negatives count as 0.
    back = decode_charge("{\"version\": 1, \"charge\": {\"x:1\": 5, \"3:\": 5, \"3:4x\": 5, \"8:9\": -20, \"1:2\" 7, \"4:5\": 6.5}}");
    assert(back.size() == 2 && back[(Key{8, 9})] == 0 && back[(Key{4, 5})] == 6.5);
    assert(decode_charge("").empty() && decode_charge("{oops").empty());
}

static void check_definition() {
    std::string j = definition_json(mesh_path());
    assert(j.find("\"id\": \"anybattery_pack\"") != std::string::npos && j.find("\"class\": \"battery\"") != std::string::npos);
    assert(j.find("\"mode_x\": \"stretch\"") != std::string::npos && j.find("\"mode_y\": \"stretch\"") != std::string::npos &&
           j.find("\"mode_z\": \"stretch\"") != std::string::npos);
    assert(j.find("\"ext_max\": [14, 14, 14]") != std::string::npos && j.find("\"interaction_zones\": []") != std::string::npos);
    assert(j.find("\"charge_percent\"") != std::string::npos && j.find("meshes/components/anybattery_pack.mesh") != std::string::npos);
    // Balanced braces and brackets, no stray quotes.
    int braces = 0, brackets = 0; size_t quotes = 0;
    for (char ch : j) { braces += ch == '{' ? 1 : ch == '}' ? -1 : 0; brackets += ch == '[' ? 1 : ch == ']' ? -1 : 0; quotes += ch == '"'; assert(braces >= 0 && brackets >= 0); }
    assert(braces == 0 && brackets == 0 && quotes % 2 == 0);
    assert(is_pack_id("anybattery_pack", 15) && !is_pack_id("battery_a", 9) && !is_pack_id("anybattery_pack2", 16));
}

static void check_mesh() {
    Mesh m = battery_pack();
    assert(!m.v.empty() && m.i.size() % 3 == 0);
    for (uint32_t i : m.i) assert(i < m.v.size());
    for (auto& v : m.v) {
        double n = std::sqrt(double(v.normal[0]) * v.normal[0] + double(v.normal[1]) * v.normal[1] + double(v.normal[2]) * v.normal[2]);
        assert(std::abs(n - 1) < 1e-3);
        for (int k = 0; k < 3; ++k) assert(v.pos[k] >= -0.0405f && v.pos[k] <= 0.1205f);   // inside the part's 2 x 2 x 2 cells
    }
    assert(clear_of_middle(m));                                // nothing on a stretch plane
    std::vector<uint8_t> file = encode_mesh(m, kMeshName);
    assert(file.size() == 16 + std::strlen(kMeshName) + 88 + 48 + 4 + m.v.size() * 36 + 4 + m.i.size() * 4 + 8 && std::memcmp(file.data(), "mesh", 4) == 0);
}

int main() {
    check_capacity();
    check_ticks();
    check_store();
    check_definition();
    check_mesh();
    std::printf("anybattery logic: ok\n");
    return 0;
}
